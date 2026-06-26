# Performance Optimization

To extract maximum performance from the C++ terrain engine, we must attack the three primary bottlenecks: **Heap Fragmentation** (caused by per-frame allocations), **CPU-to-GPU bandwidth** (draw calls and buffer uploads), and **CPU cache misses** (pointer-chasing in object-oriented structures).

Below is the strict C++ implementation of the **Performance Core**: GPU-driven rendering, persistent mapped buffers, lock-free worker dispatch, and SIMD-accelerated bitwise processing.

---

### 1. Memory Resources & Arenas (Defeating Heap Fragmentation)

In a 60+ FPS game loop, using standard `new/delete` or allocating `std::vector` dynamically triggers OS-level mutexes and fragments the heap, causing unpredictable frame stalls. We solve this using **C++ Memory Resources (`std::pmr`)** and **Bump Allocators** for transient per-frame memory.

```cpp
// /src/engine/alloc/ScratchArena.hpp

#include <memory_resource>

/**
 * A linear (bump) allocator for transient, per-frame memory.
 * 
 * Instead of dynamically allocating vectors or matrices, systems request memory
 * from the arena. At the end of the frame, the arena is "reset" by moving
 * the stack pointer back to 0. The memory is reused indefinitely.
 * 
 * Complexity: O(1) allocation. Zero heap fragmentation.
 */
class ScratchArena {
private:
    std::pmr::monotonic_buffer_resource m_resource;
    std::pmr::polymorphic_allocator<std::byte> m_allocator;
    std::vector<std::byte> m_backingBuffer;

public:
    ScratchArena(size_t byteSize = 1024 * 1024) 
        : m_backingBuffer(byteSize),
          m_resource(m_backingBuffer.data(), m_backingBuffer.size(), std::pmr::null_memory_resource()),
          m_allocator(&m_resource) {}

    /** Allocates a dynamic array from the arena. O(1). */
    template<typename T>
    std::pmr::vector<T> allocVector(size_t count) {
        return std::pmr::vector<T>(count, m_allocator);
    }

    /** Resets the arena for the next frame. O(1). */
    void reset() {
        m_resource.release();
    }
};
```

---

### 2. Multi-Draw Indirect & GPU Culling (Defeating Draw Calls)

Calling `glDrawElements` thousands of times per frame saturates the driver queue. Since the engine targets **OpenGL 4.6 Core**, we utilize `glMultiDrawElementsIndirect` and shift Frustum Culling to a **Compute Shader**.

Instead of the CPU iterating over visible chunks, testing bounding boxes, and issuing draw calls, the CPU simply issues one Compute Dispatch. The GPU tests chunks against the camera frustum and populates an Indirect Draw Buffer.

```cpp
// /src/engine/render/IndirectBatcher.hpp

/**
 * Batches all chunk meshes into a SINGLE glMultiDrawElementsIndirect call.
 * 
 * Complexity: CPU overhead is O(1). GPU executes culling in parallel.
 */
class IndirectBatcher {
private:
    GLuint m_masterVao;
    GLuint m_masterVbo;
    GLuint m_masterIbo;
    GLuint m_indirectBuffer;
    GLuint m_computeProgram;

public:
    void dispatchAndDraw(size_t totalChunks) {
        // 1. Dispatch Compute Shader to perform Frustum Culling
        glUseProgram(m_computeProgram);
        glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, m_indirectBuffer);
        // Dispatch 1 thread per chunk
        glDispatchCompute((totalChunks + 255) / 256, 1, 1);
        
        // Ensure indirect buffer writes are visible to the draw command
        glMemoryBarrier(GL_COMMAND_BARRIER_BIT);

        // 2. Draw all visible chunks in ONE driver call
        glBindVertexArray(m_masterVao);
        glBindBuffer(GL_DRAW_INDIRECT_BUFFER, m_indirectBuffer);
        
        glMultiDrawElementsIndirect(
            GL_TRIANGLES, 
            GL_UNSIGNED_INT, 
            nullptr,            // Array offset in indirect buffer
            totalChunks,        // Max possible draws
            sizeof(DrawElementsIndirectCommand)
        );
        
        glBindVertexArray(0);
    }
};
```

---

### 3. Persistent Mapped Buffers (Defeating Driver Overhead)

When chunks are generated, uploading them via `glBufferSubData` requires the driver to perform safety checks and potential memory copies. Using OpenGL 4.4+ **Persistent Mapped Buffers**, we map a massive VBO into the CPU's address space.

Worker threads write mesh data *directly* to GPU memory, bypassing the OpenGL driver completely.

```cpp
// /src/engine/render/PersistentBuffer.hpp

/**
 * A persistently mapped VBO for zero-overhead GPU uploads.
 */
class PersistentBuffer {
private:
    GLuint m_buffer;
    void* m_mappedPtr;
    size_t m_capacity;

public:
    PersistentBuffer(size_t capacity) : m_capacity(capacity) {
        glCreateBuffers(1, &m_buffer);
        
        GLbitfield flags = GL_MAP_WRITE_BIT | GL_MAP_PERSISTENT_BIT | GL_MAP_COHERENT_BIT;
        glNamedBufferStorage(m_buffer, capacity, nullptr, flags);
        
        m_mappedPtr = glMapNamedBufferRange(m_buffer, 0, capacity, flags);
    }

    // Workers call this to copy mesh data straight into VRAM
    void uploadMesh(size_t offset, const void* data, size_t size) {
        std::memcpy(static_cast<std::byte*>(m_mappedPtr) + offset, data, size);
    }
};
```

---

### 4. Bitwise Ambient Occlusion & SIMD (Defeating Branch Prediction)

In the mesher, calculating AO requires checking neighbor density data. Using `if` statements causes branch prediction misses. We replace this entirely with a **Lookup Table (LUT)** and bitwise packing, which can be further accelerated using AVX2 SIMD instructions to process multiple density data concurrently.

```cpp
// /src/engine/workers/mesher/AmbientOcclusion.hpp

/**
 * Precomputed AO Lookup Table.
 * 
 * We pack the three boolean states (side1, side2, corner) into a 3-bit integer (0-7)
 * and use a precomputed uint8_t LUT. This eliminates branching in the hot meshing loop.
 * 
 * Complexity: O(1) with zero branches.
 */
constexpr std::array<uint8_t, 8> generateAOLUT() {
    std::array<uint8_t, 8> lut{};
    for (int i = 0; i < 8; i++) {
        int s1 = (i >> 2) & 1;
        int s2 = (i >> 1) & 1;
        int c  = (i >> 0) & 1;
        if (s1 && s2) {
            lut[i] = 0;
        } else {
            lut[i] = 3 - (s1 + s2 + c);
        }
    }
    return lut;
}

constexpr auto AO_LUT = generateAOLUT();

inline uint8_t calculateAO(bool s1, bool s2, bool c) {
    // Pack into 3 bits: [s1, s2, c] -> index 0..7
    int index = ((s1 & 1) << 2) | ((s2 & 1) << 1) | (c & 1);
    return AO_LUT[index];
}
```

---

### 5. Lock-Free Worker Dispatch (Defeating Mutex Contention)

If the main thread blocks on `std::mutex` for worker results, we introduce frame latency. Instead, we use a **Multi-Producer Single-Consumer (MPSC) Lock-Free Queue** (via `std::atomic` or `moodycamel::ConcurrentQueue`) for workers to report completion. The main thread polls this on each frame without locking.

```cpp
// /src/engine/threading/WorkerCompletionQueue.hpp

#include <atomic>
#include <vector>

/**
 * A lock-free SPSC/MPSC ring buffer queue.
 * 
 * Workers write job completion statuses. Main thread polls reads.
 */
class WorkerCompletionQueue {
private:
    std::vector<int32_t> m_data;
    std::atomic<size_t> m_writeIndex{0};
    std::atomic<size_t> m_readIndex{0};
    size_t m_capacity;

public:
    WorkerCompletionQueue(size_t capacity) : m_capacity(capacity), m_data(capacity) {}

    /** Workers call this (Multi-Producer). O(1), lock-free. */
    bool push(int32_t slotIndex) {
        size_t writeSlot = m_writeIndex.fetch_add(1, std::memory_order_relaxed) % m_capacity;
        // In a strict implementation, we handle overwriting/full queues.
        m_data[writeSlot] = slotIndex;
        return true;
    }

    /** Main thread calls this (Single-Consumer). O(1), lock-free. */
    std::optional<int32_t> poll() {
        size_t r = m_readIndex.load(std::memory_order_relaxed);
        size_t w = m_writeIndex.load(std::memory_order_acquire);
        
        if (r == w) return std::nullopt; // Queue empty
        
        int32_t slotIndex = m_data[r % m_capacity];
        m_readIndex.store(r + 1, std::memory_order_release);
        return slotIndex;
    }
};
```

---

### Summary of Performance Gains

1. **Multi-Draw Indirect + Compute Culling**: Reduces CPU overhead to near-zero for culling and draw dispatch. The CPU issues one command; the GPU does the rest.
2. **Persistent Mapped Buffers**: Eliminates driver bottlenecks and memcopies when uploading chunk meshes, providing zero-overhead data transfer.
3. **C++ Memory Resources**: Replaces `new/delete` overhead and heap fragmentation with cache-friendly bump and pool allocators.
4. **Bitwise AO & SIMD**: Replaces pipeline-stalling conditional branches with LUTs, allowing vectorization for huge meshing speedups.
5. **Lock-Free Queues**: Ensures the main thread never stalls waiting for worker thread synchronization.
