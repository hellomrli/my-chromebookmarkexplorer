#!/usr/bin/env python3
"""生成简单的图标资源"""
from PIL import Image, ImageDraw

def create_app_icon():
    """应用图标 - 蓝色书签形状"""
    img = Image.new('RGBA', (256, 256), (0, 0, 0, 0))
    draw = ImageDraw.Draw(img)
    # 书签形状
    points = [(50, 30), (206, 30), (206, 226), (128, 180), (50, 226)]
    draw.polygon(points, fill='#4A90E2', outline='#2E5C8A', width=6)
    # 星标
    draw.ellipse([100, 80, 156, 136], fill='#FFD700', outline='#FFA500', width=3)
    img.save('app.png', 'PNG')
    print('✓ app.png')

def create_folder_icon():
    """文件夹图标 - 黄色文件夹"""
    img = Image.new('RGBA', (64, 64), (0, 0, 0, 0))
    draw = ImageDraw.Draw(img)
    # 文件夹主体
    draw.rounded_rectangle([8, 18, 56, 52], radius=4, fill='#FDB750', outline='#D89635', width=2)
    # 文件夹标签
    draw.rounded_rectangle([8, 14, 32, 22], radius=2, fill='#FDB750', outline='#D89635', width=2)
    img.save('folder.png', 'PNG')
    print('✓ folder.png')

def create_folder_open_icon():
    """打开的文件夹图标"""
    img = Image.new('RGBA', (64, 64), (0, 0, 0, 0))
    draw = ImageDraw.Draw(img)
    # 打开的文件夹
    draw.polygon([(8, 20), (32, 16), (58, 16), (58, 50), (10, 54)], 
                 fill='#FFCA6C', outline='#D89635', width=2)
    img.save('folder-open.png', 'PNG')
    print('✓ folder-open.png')

def create_bookmark_icon():
    """书签图标 - 蓝色链接"""
    img = Image.new('RGBA', (64, 64), (0, 0, 0, 0))
    draw = ImageDraw.Draw(img)
    # 地球
    draw.ellipse([12, 12, 52, 52], fill='#5BA3E8', outline='#3E7CB8', width=2)
    # 经线
    draw.ellipse([20, 12, 44, 52], fill=None, outline='white', width=2)
    # 纬线
    draw.ellipse([12, 24, 52, 40], fill=None, outline='white', width=2)
    img.save('bookmark.png', 'PNG')
    print('✓ bookmark.png')

if __name__ == '__main__':
    create_app_icon()
    create_folder_icon()
    create_folder_open_icon()
    create_bookmark_icon()
    print('\n所有图标生成完毕！')
