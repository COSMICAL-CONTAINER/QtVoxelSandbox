#!/usr/bin/env python3
"""misc 二轮：仅生成 icon_bedrock.png（深灰粗糙岩石立方体图标）。

复用 build_cube_icons.render（2:1 dimetric 立方体投影 + 顶/右/左三档明暗），不重跑全部图标
（避免与其余图标无关的像素差异）。已同步把 bedrock 项加入 build_cube_icons.py 的 BLOCKS 列表，
故全量重跑亦会产出本图标；本脚本仅供「单点重建」便利。

资产：源贴图 default_bedrock.png 是项目原创程序生成贴图（非 MC 资产，PLAN §9 红线）。
"""
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, "..", "tools"))

from build_cube_icons import render  # 复用同一渲染管线

SRC = os.path.join(HERE, "..", "textures")
out = os.path.join(SRC, "icon_bedrock.png")
img = render("default_bedrock", "default_bedrock")
img.save(out)
print("wrote", os.path.relpath(out, HERE), img.size)
