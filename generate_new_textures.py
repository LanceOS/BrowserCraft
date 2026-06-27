import numpy as np
from PIL import Image
import os
import random

def create_texture(name, func):
    img_array = func()
    img_array = np.clip(img_array, 0, 255).astype(np.uint8)
    img = Image.fromarray(img_array, 'RGBA')
    os.makedirs('assets/textures', exist_ok=True)
    img.save(f'assets/textures/{name}.png')

def sand():
    base = np.array([210, 190, 140, 255])
    tex = np.full((16, 16, 4), base)
    noise = np.random.randint(-15, 15, (16, 16, 3))
    tex[:, :, :3] = tex[:, :, :3] + noise
    
    # Sand grains
    for _ in range(40):
        x, y = random.randint(0, 15), random.randint(0, 15)
        if random.random() > 0.5:
            tex[y, x, :3] = tex[y, x, :3] * 0.8 # Dark grain
        else:
            tex[y, x, :3] = np.clip(tex[y, x, :3] * 1.2, 0, 255) # Light grain
    return tex

def gravel():
    base = np.array([140, 140, 140, 255])
    tex = np.full((16, 16, 4), base)
    noise = np.random.randint(-25, 25, (16, 16, 3))
    tex[:, :, :3] = tex[:, :, :3] + noise
    
    # Pebbles
    for _ in range(30):
        x, y = random.randint(0, 15), random.randint(0, 15)
        color = np.array([100, 100, 100]) + np.random.randint(-40, 40, 3)
        tex[y, x, :3] = color
        if x < 15 and y < 15:
            tex[y+1, x+1, :3] = np.clip(tex[y+1, x+1, :3] * 0.7, 0, 255) # Shadow
        if x > 0 and y > 0:
            tex[y-1, x-1, :3] = np.clip(tex[y-1, x-1, :3] * 1.3, 0, 255) # Highlight
    return tex

def clay():
    base = np.array([160, 165, 175, 255]) # Pale blue-gray
    tex = np.full((16, 16, 4), base)
    noise = np.random.randint(-5, 5, (16, 16, 3)) # Very smooth
    tex[:, :, :3] = tex[:, :, :3] + noise
    return tex

def cracked_stone():
    base = np.array([125, 125, 125, 255])
    tex = np.full((16, 16, 4), base)
    noise = np.random.randint(-15, 15, (16, 16, 3))
    tex[:, :, :3] = tex[:, :, :3] + noise
    
    # Base highlights
    for _ in range(20):
        x, y = random.randint(0, 15), random.randint(0, 15)
        tex[y, x, :3] = np.clip(tex[y, x, :3] * 1.3, 0, 255)
        
    # Deep heavy cracks
    for _ in range(4):
        x, y = random.randint(0, 15), random.randint(0, 15)
        length = random.randint(6, 12)
        dx, dy = random.choice([(1,0), (0,1), (1,1), (-1,1), (1,-1)])
        for i in range(length):
            nx, ny = x + i*dx, y + i*dy
            if 0 <= nx < 16 and 0 <= ny < 16:
                tex[ny, nx, :3] = tex[ny, nx, :3] * 0.3
                # Branching
                if random.random() > 0.6:
                    bx, by = random.choice([(1,0), (0,1), (-1,0), (0,-1)])
                    if 0 <= nx+bx < 16 and 0 <= ny+by < 16:
                        tex[ny+by, nx+bx, :3] = tex[ny+by, nx+bx, :3] * 0.4
                        
    return tex

textures = {
    'sand': sand,
    'gravel': gravel,
    'clay': clay,
    'cracked_stone': cracked_stone
}

if __name__ == '__main__':
    for name, func in textures.items():
        create_texture(name, func)
        print(f"Generated {name}.png")
