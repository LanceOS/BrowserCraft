import os
import re

directories = ['docs', 'notes']
files_to_process = ['README.md']

for d in directories:
    if os.path.exists(d):
        for f in os.listdir(d):
            if f.endswith('.md'):
                files_to_process.append(os.path.join(d, f))

# A list of exact matches and case-insensitive regexes
# Order matters: replace longer specific phrases before generic words
replacements = [
    # Specific component renames
    (re.compile(r'\bgreedy mesher\b', re.IGNORECASE), 'SurfaceNets mesher'),
    (re.compile(r'\bGreedyMesher\b'), 'SurfaceNetsMesher'),
    (re.compile(r'\bgreedy meshing\b', re.IGNORECASE), 'SurfaceNets meshing'),
    (re.compile(r'\bgreedy\b', re.IGNORECASE), 'SurfaceNets'),
    
    (re.compile(r'\bblockId\b'), 'materialId'),
    (re.compile(r'\bBlockId\b'), 'MaterialId'),
    (re.compile(r'\bBlockRegistry\b'), 'MaterialRegistry'),
    (re.compile(r'\bblock registry\b', re.IGNORECASE), 'material registry'),
    (re.compile(r'\bBlockDefinition\b'), 'MaterialDefinition'),
    (re.compile(r'\bBlockFactory\b'), 'MaterialFactory'),
    
    (re.compile(r'\bcube meshing\b', re.IGNORECASE), 'smooth terrain meshing'),
    (re.compile(r'\bcube-based\b', re.IGNORECASE), 'density-based'),
    (re.compile(r'\bcubes\b', re.IGNORECASE), 'density points'),
    (re.compile(r'\bdiscrete cube\b', re.IGNORECASE), 'continuous density field'),
    
    # Executable and target names
    (re.compile(r'\bvoxel_engine\b'), 'terrain_engine'),
    (re.compile(r'\bvoxel_app\b'), 'terrain_app'),
    (re.compile(r'\bvoxel_tests\b'), 'terrain_tests'),
    
    # Generic "voxel" replacements
    (re.compile(r'\bvoxel engine\b', re.IGNORECASE), 'terrain engine'),
    (re.compile(r'\bvoxels\b'), 'density data'),
    (re.compile(r'\bVoxels\b'), 'Density data'),
    (re.compile(r'\bvoxel\b'), 'terrain'),
    (re.compile(r'\bVoxel\b'), 'Terrain')
]

updated_count = 0

for filepath in files_to_process:
    if os.path.isfile(filepath):
        with open(filepath, 'r') as f:
            content = f.read()
            
        orig_content = content
        
        for pattern, replacement in replacements:
            # We want to preserve basic casing where possible for generic words if the replacement is completely lowercase
            if replacement.islower():
                def replacer(m):
                    word = m.group(0)
                    if word.isupper():
                        return replacement.upper()
                    elif word[0].isupper():
                        return replacement.capitalize()
                    return replacement
                content = pattern.sub(replacer, content)
            else:
                content = pattern.sub(replacement, content)
                
        if content != orig_content:
            with open(filepath, 'w') as f:
                f.write(content)
            print(f"Updated {filepath}")
            updated_count += 1

print(f"Total files updated: {updated_count}")
