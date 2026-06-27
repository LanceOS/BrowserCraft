import numpy as np
from PIL import Image
import os
import random

def generate_noise(shape, scale=1, intensity=0.1):
    noise = np.random.randn(*shape) * intensity
    return noise

def create_texture(name, func):
    img_array = func()
    img_array = np.clip(img_array, 0, 255).astype(np.uint8)
    img = Image.fromarray(img_array, 'RGBA')
    os.makedirs('assets/textures', exist_ok=True)
    img.save(f'assets/textures/{name}.png')

def dirt():
    base = np.array([101, 75, 55, 255])
    noise = np.random.randint(-15, 15, (16, 16, 3))
    tex = np.full((16, 16, 4), base)
    tex[:, :, :3] = tex[:, :, :3] + noise
    
    # Add some grit
    for _ in range(20):
        x, y = random.randint(0, 15), random.randint(0, 15)
        tex[y, x, :3] = tex[y, x, :3] * 0.8
    for _ in range(20):
        x, y = random.randint(0, 15), random.randint(0, 15)
        tex[y, x, :3] = np.clip(tex[y, x, :3] * 1.2, 0, 255)
    return tex

def stone():
    base = np.array([125, 125, 125, 255])
    tex = np.full((16, 16, 4), base)
    noise = np.random.randint(-15, 15, (16, 16, 3))
    tex[:, :, :3] = tex[:, :, :3] + noise
    
    # Add cracks
    for _ in range(5):
        x, y = random.randint(0, 15), random.randint(0, 15)
        length = random.randint(2, 5)
        dx, dy = random.choice([(1,0), (0,1), (1,1), (-1,1)])
        for i in range(length):
            nx, ny = x + i*dx, y + i*dy
            if 0 <= nx < 16 and 0 <= ny < 16:
                tex[ny, nx, :3] = tex[ny, nx, :3] * 0.6
                
    # Add highlights
    for _ in range(20):
        x, y = random.randint(0, 15), random.randint(0, 15)
        tex[y, x, :3] = np.clip(tex[y, x, :3] * 1.3, 0, 255)
    return tex

def grass_top():
    base = np.array([85, 155, 60, 255])
    tex = np.full((16, 16, 4), base)
    noise = np.random.randint(-12, 12, (16, 16, 3))
    tex[:, :, :3] = tex[:, :, :3] + noise
    
    # Add grass blades
    for _ in range(40):
        x, y = random.randint(0, 15), random.randint(1, 15)
        tex[y, x, :3] = np.clip(tex[y, x, :3] * 1.2, 0, 255)
        tex[y-1, x, :3] = np.clip(tex[y-1, x, :3] * 1.1, 0, 255)
    return tex

def grass_side():
    d = dirt()
    gt = grass_top()
    tex = np.copy(d)
    
    # Grass overhang
    overhang = [random.randint(3, 6) for _ in range(16)]
    for x in range(16):
        for y in range(overhang[x]):
            tex[y, x] = gt[y, x]
        # Shadow under overhang
        if overhang[x] < 16:
            tex[overhang[x], x, :3] = tex[overhang[x], x, :3] * 0.5
    return tex

def oak_log_top():
    base = np.array([170, 135, 80, 255]) # Inner wood
    tex = np.full((16, 16, 4), base)
    noise = np.random.randint(-10, 10, (16, 16, 3))
    tex[:, :, :3] = tex[:, :, :3] + noise
    
    # Rings
    center = (7.5, 7.5)
    for y in range(16):
        for x in range(16):
            dist = np.sqrt((x - center[0])**2 + (y - center[1])**2)
            if int(dist * 1.5) % 2 == 0:
                tex[y, x, :3] = tex[y, x, :3] * 0.9
            
            # Bark border
            if dist > 6.5:
                tex[y, x] = np.array([90, 65, 45, 255]) + np.append(np.random.randint(-15, 15, 3), 0)
    return tex

def oak_log_side():
    base = np.array([90, 65, 45, 255]) # Bark
    tex = np.full((16, 16, 4), base)
    
    # Vertical bark lines
    for x in range(16):
        intensity = random.uniform(0.7, 1.2)
        tex[:, x, :3] = tex[:, x, :3] * intensity
        
    noise = np.random.randint(-15, 15, (16, 16, 3))
    tex[:, :, :3] = tex[:, :, :3] + noise
    
    # Deep cracks
    for _ in range(6):
        x = random.randint(0, 15)
        start_y = random.randint(0, 10)
        length = random.randint(4, 10)
        for i in range(length):
            if start_y + i < 16:
                tex[start_y + i, x, :3] = tex[start_y + i, x, :3] * 0.5
    return tex

def oak_planks():
    base = np.array([160, 125, 75, 255])
    tex = np.full((16, 16, 4), base)
    noise = np.random.randint(-8, 8, (16, 16, 3))
    tex[:, :, :3] = tex[:, :, :3] + noise
    
    # 4 horizontal planks
    for i in range(4):
        plank_y = i * 4
        # Gap
        tex[plank_y, :, :3] = tex[plank_y, :, :3] * 0.4
        # Highlight
        tex[plank_y + 1, :, :3] = np.clip(tex[plank_y + 1, :, :3] * 1.2, 0, 255)
        
        # Wood grain
        for _ in range(8):
            x, y_offset = random.randint(0, 15), random.randint(1, 3)
            length = random.randint(3, 8)
            for j in range(length):
                nx = (x + j) % 16
                tex[plank_y + y_offset, nx, :3] = tex[plank_y + y_offset, nx, :3] * 0.85
                
    return tex

def oak_leaves():
    tex = np.zeros((16, 16, 4), dtype=np.float32)
    base_color = np.array([55, 125, 40])
    
    # Generate clumps
    for _ in range(25):
        cx, cy = random.randint(0, 15), random.randint(0, 15)
        radius = random.uniform(1.5, 3.0)
        color = base_color + np.random.randint(-20, 20, 3)
        for y in range(16):
            for x in range(16):
                if (x - cx)**2 + (y - cy)**2 <= radius**2:
                    # Give it a blocky feel by occasionally skipping pixels at edge
                    if random.random() > 0.1:
                        tex[y, x, :3] = color
                        tex[y, x, 3] = 255
                        
    # Transparent holes (not fully opaque)
    for _ in range(30):
        x, y = random.randint(0, 15), random.randint(0, 15)
        tex[y, x, 3] = 0
        
    return tex

def water():
    base = np.array([40, 80, 220, 200]) # Semi-transparent blue
    tex = np.full((16, 16, 4), base)
    
    # Water patterns
    for _ in range(15):
        y = random.randint(0, 15)
        x = random.randint(0, 12)
        length = random.randint(3, 6)
        tex[y, x:x+length, :3] = np.clip(tex[y, x:x+length, :3] * 1.3, 0, 255)
        
    for _ in range(15):
        y = random.randint(0, 15)
        x = random.randint(0, 12)
        length = random.randint(3, 6)
        tex[y, x:x+length, :3] = tex[y, x:x+length, :3] * 0.7
        
    return tex

def lava():
    base = np.array([220, 80, 20, 255])
    tex = np.full((16, 16, 4), base)
    
    # Lava blobs
    for _ in range(15):
        cx, cy = random.randint(0, 15), random.randint(0, 15)
        radius = random.uniform(1.0, 3.5)
        # Yellowish
        color = np.array([255, 180, 30]) + np.random.randint(-15, 15, 3)
        for y in range(16):
            for x in range(16):
                if (x - cx)**2 + (y - cy)**2 <= radius**2:
                    tex[y, x, :3] = color
                    
    # Darker cool spots
    for _ in range(10):
        cx, cy = random.randint(0, 15), random.randint(0, 15)
        radius = random.uniform(1.0, 2.5)
        color = np.array([130, 30, 10]) + np.random.randint(-10, 10, 3)
        for y in range(16):
            for x in range(16):
                if (x - cx)**2 + (y - cy)**2 <= radius**2:
                    tex[y, x, :3] = color
                    
    return tex

def make_ore(ore_name, color, cluster_size=3, num_clusters=6):
    tex = stone()
    for _ in range(num_clusters):
        cx, cy = random.randint(2, 13), random.randint(2, 13)
        for _ in range(cluster_size):
            x = cx + random.randint(-1, 1)
            y = cy + random.randint(-1, 1)
            if 0 <= x < 16 and 0 <= y < 16:
                tex[y, x, :3] = color + np.random.randint(-20, 20, 3)
                # Highlight on top-left of the ore bit
                if y > 0 and x > 0:
                    tex[y-1, x-1, :3] = np.clip(tex[y-1, x-1, :3] + 40, 0, 255)
                # Shadow on bottom-right
                if y < 15 and x < 15:
                    tex[y+1, x+1, :3] = np.clip(tex[y+1, x+1, :3] - 40, 0, 255)
    return tex

def coal_ore(): return make_ore("coal", np.array([40, 40, 40]), 4, 7)
def iron_ore(): return make_ore("iron", np.array([200, 170, 140]), 3, 6)
def gold_ore(): return make_ore("gold", np.array([255, 215, 0]), 3, 5)
def diamond_ore(): return make_ore("diamond", np.array([80, 220, 255]), 2, 4)
def powerstone_ore(): return make_ore("powerstone", np.array([220, 40, 40]), 3, 5) # Reddish? wait, in AssetManager, it says powerstone is purple (0xFF9B30FF) which is R=155, G=48, B=255. Let's use purple/magenta.

def powerstone_ore_fixed(): return make_ore("powerstone", np.array([170, 40, 255]), 3, 6)

textures = {
    'dirt': dirt,
    'stone': stone,
    'grass_top': grass_top,
    'grass_side': grass_side,
    'oak_log_top': oak_log_top,
    'oak_log_side': oak_log_side,
    'oak_planks': oak_planks,
    'oak_leaves': oak_leaves,
    'water': water,
    'lava': lava,
    'coal_ore': coal_ore,
    'iron_ore': iron_ore,
    'gold_ore': gold_ore,
    'diamond_ore': diamond_ore,
    'powerstone_ore': powerstone_ore_fixed
}

if __name__ == '__main__':
    for name, func in textures.items():
        create_texture(name, func)
        print(f"Generated {name}.png")
