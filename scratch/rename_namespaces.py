import os
import re

# We will traverse src/ and tests/ and CMakeLists.txt and rename:
# - voxel -> terrain
# - Voxel -> Terrain
# - VOXEL -> Terrain (or TERRAIN)
# - blockId -> materialId
# - BlockId -> MaterialId
# - BLOCKID -> MATERIALID

replacements = [
    (r'\bnamespace voxel\b', 'namespace terrain'),
    (r'\bvoxel::\b', 'terrain::'),
    (r'\busing namespace voxel\b', 'using namespace terrain'),
    (r'\bvoxel\b', 'terrain'),
    (r'\bVoxel\b', 'Terrain'),
    (r'\bVOXEL\b', 'TERRAIN'),
    (r'\bblockId\b', 'materialId'),
    (r'\bBlockId\b', 'MaterialId'),
    (r'\bBLOCKID\b', 'MATERIALID'),
    (r'\bblockIds\b', 'materialIds'),
    (r'\bBlockIds\b', 'MaterialIds'),
]

def process_file(filepath):
    with open(filepath, 'r', encoding='utf-8', errors='ignore') as f:
        content = f.read()
    
    new_content = content
    for pattern, repl in replacements:
        new_content = re.sub(pattern, repl, new_content)
        
    if new_content != content:
        with open(filepath, 'w', encoding='utf-8') as f:
            f.write(new_content)
        print(f"Updated: {filepath}")

def main():
    root_dirs = ['src', 'tests']
    for root_dir in root_dirs:
        for dirpath, _, filenames in os.walk(root_dir):
            for filename in filenames:
                if filename.endswith(('.hpp', '.cpp', '.h', '.c')):
                    process_file(os.path.join(dirpath, filename))
                    
    # Also process CMakeLists.txt
    if os.path.exists('CMakeLists.txt'):
        process_file('CMakeLists.txt')

if __name__ == '__main__':
    main()
