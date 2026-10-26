#!/usr/bin/env python3
"""Generate the Chinese AutoVO dendrogram/query-protocol design document.

The workspace does not provide python-docx, Pandoc, or LibreOffice.  This
script therefore emits a standards-compliant DOCX package directly with the
Python standard library and generates its figures with Matplotlib.
"""

from __future__ import annotations

import math
import os
import struct
import zipfile
from datetime import datetime, timezone
from pathlib import Path
from xml.sax.saxutils import escape

import matplotlib

matplotlib.use("Agg")
import matplotlib.pyplot as plt
from matplotlib.patches import FancyArrowPatch, FancyBboxPatch


BASE = Path(__file__).resolve().parent
ASSET_DIR = BASE / "figures_adaptive_tree"
OUT = BASE / "Adaptive_Verifiable_ANN_Tree_Design_CN.docx"

NS_W = "http://schemas.openxmlformats.org/wordprocessingml/2006/main"
NS_R = "http://schemas.openxmlformats.org/officeDocument/2006/relationships"
NS_WP = "http://schemas.openxmlformats.org/drawingml/2006/wordprocessingDrawing"
NS_A = "http://schemas.openxmlformats.org/drawingml/2006/main"
NS_PIC = "http://schemas.openxmlformats.org/drawingml/2006/picture"


def box(ax, xy, wh, text, fc="#EAF2F8", ec="#2E86C1", fontsize=10,
        lw=1.6, radius=0.02, color="#17202A"):
    x, y = xy
    w, h = wh
    p = FancyBboxPatch(
        (x, y), w, h,
        boxstyle=f"round,pad=0.012,rounding_size={radius}",
        facecolor=fc, edgecolor=ec, linewidth=lw,
    )
    ax.add_patch(p)
    ax.text(x + w / 2, y + h / 2, text, ha="center", va="center",
            fontsize=fontsize, color=color, family="DejaVu Sans")
    return p


def arrow(ax, start, end, color="#566573", lw=1.5, style="-|>", rad=0.0):
    a = FancyArrowPatch(
        start, end, arrowstyle=style, mutation_scale=13,
        linewidth=lw, color=color,
        connectionstyle=f"arc3,rad={rad}",
    )
    ax.add_patch(a)
    return a


def setup_ax(figsize=(13, 7.2)):
    fig, ax = plt.subplots(figsize=figsize, dpi=180)
    ax.set_xlim(0, 1)
    ax.set_ylim(0, 1)
    ax.axis("off")
    fig.patch.set_facecolor("white")
    return fig, ax


def fig_construction(path: Path):
    fig, ax = setup_ax((14, 6.5))
    ax.text(0.5, 0.95, "Unified Proof-Aware Index Construction",
            ha="center", va="center", fontsize=18, weight="bold",
            color="#154360")
    labels = [
        ("Raw vectors", "X, object blocks"),
        ("Atomic micro-clusters", "bounded leaf bytes"),
        ("Proof-aware merges", "geometry + residual + auth"),
        ("Full dendrogram", "multi-resolution scaffold"),
        ("VO dynamic pruning", "keep only positive gates"),
        ("Committed index", "codes + radii + Merkle roots"),
    ]
    xs = [0.02, 0.185, 0.35, 0.515, 0.68, 0.845]
    colors = ["#FDF2E9", "#EBF5FB", "#E8F8F5", "#F4ECF7", "#FEF9E7", "#EAF2F8"]
    edges = ["#CA6F1E", "#2874A6", "#148F77", "#7D3C98", "#B7950B", "#21618C"]
    for i, ((title, sub), x) in enumerate(zip(labels, xs)):
        box(ax, (x, 0.55), (0.135, 0.22), f"{i+1}. {title}\n\n{sub}",
            fc=colors[i], ec=edges[i], fontsize=9.5)
        if i < len(labels) - 1:
            arrow(ax, (x + 0.137, 0.66), (xs[i + 1] - 0.006, 0.66))
    box(ax, (0.13, 0.17), (0.31, 0.19),
        "One construction objective\nmin VO(T, policy)\nsubject to calibrated Recall",
        fc="#F8F9F9", ec="#5D6D7E", fontsize=11)
    box(ax, (0.56, 0.17), (0.31, 0.19),
        "One gate intuition\nbytes hidden below the gate\n− bytes paid to authenticate it",
        fc="#F8F9F9", ec="#5D6D7E", fontsize=11)
    arrow(ax, (0.44, 0.265), (0.56, 0.265), color="#117864", lw=2.0)
    ax.text(0.5, 0.08,
            "The micro-cluster count is a physical block-layout decision; the effective leaf count, depth and fanout emerge after pruning.",
            ha="center", fontsize=10, color="#566573")
    fig.tight_layout()
    fig.savefig(path, bbox_inches="tight")
    plt.close(fig)


def draw_tree(ax, nodes, edges, title, expanded=None, scanned=None, collapsed=None):
    expanded = set(expanded or [])
    scanned = set(scanned or [])
    collapsed = set(collapsed or [])
    for a, b in edges:
        x1, y1 = nodes[a][0], nodes[a][1]
        x2, y2 = nodes[b][0], nodes[b][1]
        arrow(ax, (x1, y1 - 0.026), (x2, y2 + 0.026),
              color="#AAB7B8" if b in collapsed else "#5D6D7E", lw=1.2)
    for name, (x, y) in nodes.items():
        if name in scanned:
            fc, ec = "#D5F5E3", "#1E8449"
        elif name in expanded:
            fc, ec = "#D6EAF8", "#2874A6"
        elif name in collapsed:
            fc, ec = "#F2F3F4", "#AAB7B8"
        else:
            fc, ec = "#FCF3CF", "#B7950B"
        box(ax, (x - 0.035, y - 0.025), (0.07, 0.05), name,
            fc=fc, ec=ec, fontsize=8, lw=1.2, radius=0.008)
    ax.text(0.5, 0.98, title, transform=ax.transAxes, ha="center",
            fontsize=13, weight="bold", color="#154360")
    ax.axis("off")


def fig_dendrogram(path: Path):
    fig, axes = plt.subplots(1, 2, figsize=(14, 7.3), dpi=180)
    fig.patch.set_facecolor("white")
    full_nodes = {
        "Root": (0.50, 0.88), "P0": (0.27, 0.69), "P1": (0.73, 0.69),
        "G0": (0.13, 0.49), "G1": (0.39, 0.49), "G2": (0.61, 0.49), "G3": (0.87, 0.49),
        "M0": (0.06, 0.23), "M1": (0.19, 0.23), "M2": (0.32, 0.23), "M3": (0.45, 0.23),
        "M4": (0.55, 0.23), "M5": (0.68, 0.23), "M6": (0.81, 0.23), "M7": (0.94, 0.23),
    }
    full_edges = [
        ("Root", "P0"), ("Root", "P1"), ("P0", "G0"), ("P0", "G1"),
        ("P1", "G2"), ("P1", "G3"), ("G0", "M0"), ("G0", "M1"),
        ("G1", "M2"), ("G1", "M3"), ("G2", "M4"), ("G2", "M5"),
        ("G3", "M6"), ("G3", "M7"),
    ]
    draw_tree(axes[0], full_nodes, full_edges, "Full Dendrogram")
    axes[0].text(0.5, 0.07, "Every merge is retained in the scaffold", ha="center",
                 fontsize=9, color="#566573")

    pruned_nodes = {
        "Root": (0.50, 0.88), "P0": (0.27, 0.68), "P1": (0.73, 0.68),
        "G0": (0.13, 0.47), "M2": (0.33, 0.47), "M3": (0.46, 0.47),
        "G2": (0.61, 0.47), "M6": (0.81, 0.47), "M7": (0.94, 0.47),
        "M0": (0.075, 0.22), "M1": (0.185, 0.22), "M4": (0.555, 0.22), "M5": (0.665, 0.22),
    }
    pruned_edges = [
        ("Root", "P0"), ("Root", "P1"), ("P0", "G0"), ("P0", "M2"), ("P0", "M3"),
        ("P1", "G2"), ("P1", "M6"), ("P1", "M7"), ("G0", "M0"), ("G0", "M1"),
        ("G2", "M4"), ("G2", "M5"),
    ]
    draw_tree(axes[1], pruned_nodes, pruned_edges, "VO-Pruned Effective Tree",
              expanded={"Root", "P0", "P1", "G0", "G2"})
    axes[1].text(0.5, 0.07, "G1 and G3 are bypassed because their authentication gates have non-positive gain",
                 ha="center", fontsize=9, color="#566573")
    fig.tight_layout(w_pad=2.5)
    fig.savefig(path, bbox_inches="tight")
    plt.close(fig)


def fig_query_protocol(path: Path):
    fig, ax = setup_ax((11, 9.2))
    ax.text(0.5, 0.96, "Authenticated Best-First Query State Machine",
            ha="center", fontsize=17, weight="bold", color="#154360")
    ys = [0.84, 0.68, 0.52, 0.36, 0.20]
    labels = [
        "Authenticate root descriptor block\ninitialize frontier priority queue",
        "Pop node with minimum certified lower bound LB(q,u)",
        "Internal node: reveal full child block\nLeaf: verify object block and exact distances",
        "Update frontier, top-k and current kth distance tau",
        "Stop when min(frontier LB) >= gamma * tau",
    ]
    fills = ["#EBF5FB", "#F4ECF7", "#E8F8F5", "#FEF9E7", "#FDEDEC"]
    edges = ["#2874A6", "#7D3C98", "#148F77", "#B7950B", "#B03A2E"]
    for i, (y, label) in enumerate(zip(ys, labels)):
        box(ax, (0.23, y - 0.06), (0.54, 0.12), label,
            fc=fills[i], ec=edges[i], fontsize=10.5)
        if i < len(labels) - 1:
            arrow(ax, (0.5, y - 0.063), (0.5, ys[i + 1] + 0.064))
    arrow(ax, (0.77, 0.20), (0.87, 0.20), color="#B03A2E")
    box(ax, (0.87, 0.15), (0.11, 0.10), "YES\nreturn VO", fc="#FADBD8", ec="#B03A2E", fontsize=9)
    arrow(ax, (0.23, 0.20), (0.09, 0.20), color="#2874A6")
    arrow(ax, (0.09, 0.20), (0.09, 0.68), color="#2874A6")
    arrow(ax, (0.09, 0.68), (0.23, 0.68), color="#2874A6")
    ax.text(0.105, 0.43, "NO:\ncontinue", ha="center", va="center", fontsize=9,
            color="#2874A6")
    ax.text(0.5, 0.055,
            "The verifier replays the same queue, tie-breaking rule, distance updates and stopping predicate.",
            ha="center", fontsize=10, color="#566573")
    fig.tight_layout()
    fig.savefig(path, bbox_inches="tight")
    plt.close(fig)


def fig_query_example(path: Path):
    fig, ax = setup_ax((14, 7.4))
    ax.text(0.5, 0.96, "Illustrative Query: q=(1.2,1.0), k=3, gamma=0.88",
            ha="center", fontsize=16, weight="bold", color="#154360")
    nodes = {
        "Root": (0.50, 0.84), "P0\nLB=0.00": (0.28, 0.66), "P1\nLB=6.20": (0.75, 0.66),
        "G0\nLB=0.05": (0.12, 0.44), "M2\nLB=2.10": (0.30, 0.44), "M3\nLB=2.90": (0.45, 0.44),
        "G2": (0.66, 0.44), "M6": (0.82, 0.44), "M7": (0.94, 0.44),
        "M0\nLB=0.10": (0.07, 0.20), "M1\nLB=0.55": (0.18, 0.20),
    }
    edges = [
        ("Root", "P0\nLB=0.00"), ("Root", "P1\nLB=6.20"),
        ("P0\nLB=0.00", "G0\nLB=0.05"), ("P0\nLB=0.00", "M2\nLB=2.10"),
        ("P0\nLB=0.00", "M3\nLB=2.90"), ("P1\nLB=6.20", "G2"),
        ("P1\nLB=6.20", "M6"), ("P1\nLB=6.20", "M7"),
        ("G0\nLB=0.05", "M0\nLB=0.10"), ("G0\nLB=0.05", "M1\nLB=0.55"),
    ]
    expanded = {"Root", "P0\nLB=0.00", "G0\nLB=0.05"}
    scanned = {"M0\nLB=0.10", "M1\nLB=0.55"}
    collapsed = {"P1\nLB=6.20", "G2", "M6", "M7", "M2\nLB=2.10", "M3\nLB=2.90"}
    for a, b in edges:
        x1, y1 = nodes[a]
        x2, y2 = nodes[b]
        arrow(ax, (x1, y1 - 0.033), (x2, y2 + 0.033),
              color="#AAB7B8" if b in collapsed else "#2874A6", lw=1.4)
    for name, (x, y) in nodes.items():
        if name in scanned:
            fc, ec = "#D5F5E3", "#1E8449"
        elif name in expanded:
            fc, ec = "#D6EAF8", "#2874A6"
        elif name in collapsed:
            fc, ec = "#F2F3F4", "#AAB7B8"
        else:
            fc, ec = "#FCF3CF", "#B7950B"
        box(ax, (x - 0.055, y - 0.035), (0.11, 0.07), name,
            fc=fc, ec=ec, fontsize=8.2, lw=1.3, radius=0.008)
    box(ax, (0.34, 0.08), (0.32, 0.08),
        "After scanning M0 and M1: tau=0.78\nmin frontier LB=2.10 >= 0.88*0.78=0.686 -> STOP",
        fc="#FDEDEC", ec="#B03A2E", fontsize=9.5)
    ax.text(0.02, 0.08, "Blue: expanded", fontsize=9, color="#2874A6")
    ax.text(0.02, 0.045, "Green: leaf scanned", fontsize=9, color="#1E8449")
    ax.text(0.78, 0.08, "Gray: authenticated frontier, not expanded", fontsize=9, color="#7B7D7D")
    fig.tight_layout()
    fig.savefig(path, bbox_inches="tight")
    plt.close(fig)


def png_size(path: Path):
    data = path.read_bytes()
    if data[:8] != b"\x89PNG\r\n\x1a\n":
        raise ValueError(f"not PNG: {path}")
    return struct.unpack(">II", data[16:24])


def rpr(bold=False, italic=False, color=None, size=None, font=None):
    out = ["<w:rPr>"]
    if bold:
        out.append("<w:b/>")
    if italic:
        out.append("<w:i/>")
    if color:
        out.append(f'<w:color w:val="{color}"/>')
    if size:
        out.append(f'<w:sz w:val="{int(size * 2)}"/><w:szCs w:val="{int(size * 2)}"/>')
    if font:
        out.append(f'<w:rFonts w:ascii="{font}" w:hAnsi="{font}" w:eastAsia="Microsoft YaHei"/>')
    out.append("</w:rPr>")
    return "".join(out)


class DocBuilder:
    def __init__(self):
        self.body = []
        self.images = []
        self.doc_rels = [
            ("rId1", "http://schemas.openxmlformats.org/officeDocument/2006/relationships/styles", "styles.xml", None),
            ("rId2", "http://schemas.openxmlformats.org/officeDocument/2006/relationships/settings", "settings.xml", None),
            ("rId3", "http://schemas.openxmlformats.org/officeDocument/2006/relationships/footer", "footer1.xml", None),
        ]
        self.next_rel = 10
        self.next_docpr = 1

    def paragraph(self, text="", style=None, bold=False, italic=False,
                  color=None, size=None, align=None, keep=False, before=0, after=None,
                  font=None, indent=None, border=None):
        ppr = []
        if style:
            ppr.append(f'<w:pStyle w:val="{style}"/>')
        if align:
            ppr.append(f'<w:jc w:val="{align}"/>')
        if keep:
            ppr.append("<w:keepNext/>")
        if indent:
            ppr.append(f'<w:ind w:left="{indent}"/>')
        spacing = []
        if before:
            spacing.append(f'w:before="{before}"')
        if after is not None:
            spacing.append(f'w:after="{after}"')
        if spacing:
            ppr.append("<w:spacing " + " ".join(spacing) + "/>")
        if border:
            ppr.append(f'<w:pBdr><w:left w:val="single" w:sz="18" w:space="10" w:color="{border}"/></w:pBdr>')
        ppr_xml = "<w:pPr>" + "".join(ppr) + "</w:pPr>" if ppr else ""
        chunks = str(text).split("\n")
        runs = []
        for i, chunk in enumerate(chunks):
            if i:
                runs.append("<w:r><w:br/></w:r>")
            preserve = ' xml:space="preserve"' if chunk.startswith(" ") or chunk.endswith(" ") else ""
            runs.append(f"<w:r>{rpr(bold, italic, color, size, font)}<w:t{preserve}>{escape(chunk)}</w:t></w:r>")
        self.body.append("<w:p>" + ppr_xml + "".join(runs) + "</w:p>")

    def rich_paragraph(self, runs, style=None, align=None, after=None):
        ppr = []
        if style:
            ppr.append(f'<w:pStyle w:val="{style}"/>')
        if align:
            ppr.append(f'<w:jc w:val="{align}"/>')
        if after is not None:
            ppr.append(f'<w:spacing w:after="{after}"/>')
        out = ["<w:p>"]
        if ppr:
            out.append("<w:pPr>" + "".join(ppr) + "</w:pPr>")
        for text, opts in runs:
            opts = opts or {}
            out.append(f"<w:r>{rpr(**opts)}<w:t xml:space=\"preserve\">{escape(text)}</w:t></w:r>")
        out.append("</w:p>")
        self.body.append("".join(out))

    def bullet(self, text, level=0):
        self.paragraph("• " + text, indent=360 + level * 360, after=80)

    def code(self, text):
        self.paragraph(text, style="Code", font="Consolas", size=9, after=80)

    def page_break(self):
        self.body.append('<w:p><w:r><w:br w:type="page"/></w:r></w:p>')

    def table(self, headers, rows, widths=None, header_fill="D9EAF7", font_size=8.7):
        n = len(headers)
        if widths is None:
            widths = [int(9000 / n)] * n
        tbl = [
            '<w:tbl><w:tblPr><w:tblW w:w="0" w:type="auto"/>',
            '<w:tblBorders><w:top w:val="single" w:sz="6" w:color="AAB7B8"/>',
            '<w:left w:val="single" w:sz="6" w:color="AAB7B8"/>',
            '<w:bottom w:val="single" w:sz="6" w:color="AAB7B8"/>',
            '<w:right w:val="single" w:sz="6" w:color="AAB7B8"/>',
            '<w:insideH w:val="single" w:sz="4" w:color="D5D8DC"/>',
            '<w:insideV w:val="single" w:sz="4" w:color="D5D8DC"/></w:tblBorders>',
            '<w:tblCellMar><w:top w:w="80" w:type="dxa"/><w:left w:w="100" w:type="dxa"/>',
            '<w:bottom w:w="80" w:type="dxa"/><w:right w:w="100" w:type="dxa"/></w:tblCellMar>',
            '</w:tblPr><w:tblGrid>',
        ]
        for width in widths:
            tbl.append(f'<w:gridCol w:w="{width}"/>')
        tbl.append("</w:tblGrid>")

        def row_xml(values, header=False):
            out = ["<w:tr>"]
            for value, width in zip(values, widths):
                shade = f'<w:shd w:fill="{header_fill}"/>' if header else ""
                out.append(
                    f'<w:tc><w:tcPr><w:tcW w:w="{width}" w:type="dxa"/>{shade}<w:vAlign w:val="center"/></w:tcPr>'
                    f'<w:p><w:pPr><w:spacing w:after="0"/></w:pPr><w:r>{rpr(bold=header, size=font_size)}'
                    f'<w:t>{escape(str(value))}</w:t></w:r></w:p></w:tc>'
                )
            out.append("</w:tr>")
            return "".join(out)

        tbl.append(row_xml(headers, True))
        for row in rows:
            tbl.append(row_xml(row, False))
        tbl.append("</w:tbl>")
        self.body.append("".join(tbl))
        self.paragraph("", after=40)

    def image(self, path: Path, width_inches=6.5, caption=None):
        rid = f"rId{self.next_rel}"
        self.next_rel += 1
        name = f"image{len(self.images)+1}.png"
        self.images.append((name, path))
        self.doc_rels.append((rid, "http://schemas.openxmlformats.org/officeDocument/2006/relationships/image", f"media/{name}", None))
        pxw, pxh = png_size(path)
        cx = int(width_inches * 914400)
        cy = int(cx * pxh / pxw)
        docpr = self.next_docpr
        self.next_docpr += 1
        xml = f'''<w:p><w:pPr><w:jc w:val="center"/><w:spacing w:after="80"/></w:pPr><w:r><w:drawing>
<wp:inline distT="0" distB="0" distL="0" distR="0"><wp:extent cx="{cx}" cy="{cy}"/><wp:effectExtent l="0" t="0" r="0" b="0"/>
<wp:docPr id="{docpr}" name="{name}"/><wp:cNvGraphicFramePr><a:graphicFrameLocks noChangeAspect="1"/></wp:cNvGraphicFramePr>
<a:graphic><a:graphicData uri="http://schemas.openxmlformats.org/drawingml/2006/picture"><pic:pic>
<pic:nvPicPr><pic:cNvPr id="0" name="{name}"/><pic:cNvPicPr/></pic:nvPicPr>
<pic:blipFill><a:blip r:embed="{rid}"/><a:stretch><a:fillRect/></a:stretch></pic:blipFill>
<pic:spPr><a:xfrm><a:off x="0" y="0"/><a:ext cx="{cx}" cy="{cy}"/></a:xfrm><a:prstGeom prst="rect"><a:avLst/></a:prstGeom></pic:spPr>
</pic:pic></a:graphicData></a:graphic></wp:inline></w:drawing></w:r></w:p>'''
        self.body.append(xml)
        if caption:
            self.paragraph(caption, style="Caption", align="center", after=120)

    def build_document_xml(self):
        sect = f'''<w:sectPr><w:footerReference w:type="default" r:id="rId3"/>
<w:pgSz w:w="11906" w:h="16838"/><w:pgMar w:top="1134" w:right="1134" w:bottom="1134" w:left="1134" w:header="600" w:footer="600" w:gutter="0"/>
<w:cols w:space="720"/><w:docGrid w:linePitch="312"/></w:sectPr>'''
        return f'''<?xml version="1.0" encoding="UTF-8" standalone="yes"?>
<w:document xmlns:w="{NS_W}" xmlns:r="{NS_R}" xmlns:wp="{NS_WP}" xmlns:a="{NS_A}" xmlns:pic="{NS_PIC}">
<w:body>{''.join(self.body)}{sect}</w:body></w:document>'''


def styles_xml():
    return f'''<?xml version="1.0" encoding="UTF-8" standalone="yes"?>
<w:styles xmlns:w="{NS_W}">
<w:docDefaults><w:rPrDefault><w:rPr><w:rFonts w:ascii="Aptos" w:hAnsi="Aptos" w:eastAsia="Microsoft YaHei"/><w:sz w:val="21"/><w:szCs w:val="21"/><w:lang w:val="en-US" w:eastAsia="zh-CN"/></w:rPr></w:rPrDefault>
<w:pPrDefault><w:pPr><w:spacing w:after="120" w:line="330" w:lineRule="auto"/></w:pPr></w:pPrDefault></w:docDefaults>
<w:style w:type="paragraph" w:default="1" w:styleId="Normal"><w:name w:val="Normal"/><w:qFormat/></w:style>
<w:style w:type="paragraph" w:styleId="Title"><w:name w:val="Title"/><w:basedOn w:val="Normal"/><w:next w:val="Subtitle"/><w:qFormat/><w:pPr><w:spacing w:before="200" w:after="240"/><w:jc w:val="center"/></w:pPr><w:rPr><w:rFonts w:ascii="Aptos Display" w:hAnsi="Aptos Display" w:eastAsia="Microsoft YaHei"/><w:b/><w:color w:val="17365D"/><w:sz w:val="42"/><w:szCs w:val="42"/></w:rPr></w:style>
<w:style w:type="paragraph" w:styleId="Subtitle"><w:name w:val="Subtitle"/><w:basedOn w:val="Normal"/><w:qFormat/><w:pPr><w:spacing w:after="220"/><w:jc w:val="center"/></w:pPr><w:rPr><w:color w:val="5B6573"/><w:sz w:val="24"/><w:szCs w:val="24"/></w:rPr></w:style>
<w:style w:type="paragraph" w:styleId="Heading1"><w:name w:val="heading 1"/><w:basedOn w:val="Normal"/><w:next w:val="Normal"/><w:qFormat/><w:pPr><w:keepNext/><w:pageBreakBefore/><w:spacing w:before="240" w:after="140"/><w:outlineLvl w:val="0"/></w:pPr><w:rPr><w:b/><w:color w:val="17365D"/><w:sz w:val="30"/><w:szCs w:val="30"/></w:rPr></w:style>
<w:style w:type="paragraph" w:styleId="Heading2"><w:name w:val="heading 2"/><w:basedOn w:val="Normal"/><w:next w:val="Normal"/><w:qFormat/><w:pPr><w:keepNext/><w:spacing w:before="220" w:after="100"/><w:outlineLvl w:val="1"/></w:pPr><w:rPr><w:b/><w:color w:val="1F4E79"/><w:sz w:val="25"/><w:szCs w:val="25"/></w:rPr></w:style>
<w:style w:type="paragraph" w:styleId="Heading3"><w:name w:val="heading 3"/><w:basedOn w:val="Normal"/><w:next w:val="Normal"/><w:qFormat/><w:pPr><w:keepNext/><w:spacing w:before="160" w:after="80"/><w:outlineLvl w:val="2"/></w:pPr><w:rPr><w:b/><w:color w:val="2E75B6"/><w:sz w:val="22"/><w:szCs w:val="22"/></w:rPr></w:style>
<w:style w:type="paragraph" w:styleId="Caption"><w:name w:val="Caption"/><w:basedOn w:val="Normal"/><w:qFormat/><w:pPr><w:spacing w:after="120"/><w:jc w:val="center"/></w:pPr><w:rPr><w:i/><w:color w:val="666666"/><w:sz w:val="18"/><w:szCs w:val="18"/></w:rPr></w:style>
<w:style w:type="paragraph" w:styleId="Code"><w:name w:val="Code"/><w:basedOn w:val="Normal"/><w:pPr><w:spacing w:before="80" w:after="80" w:line="260" w:lineRule="auto"/><w:ind w:left="360" w:right="180"/><w:shd w:fill="F4F6F6"/><w:pBdr><w:left w:val="single" w:sz="12" w:space="8" w:color="5DADE2"/></w:pBdr></w:pPr><w:rPr><w:rFonts w:ascii="Consolas" w:hAnsi="Consolas" w:eastAsia="Microsoft YaHei"/><w:sz w:val="18"/><w:szCs w:val="18"/></w:rPr></w:style>
</w:styles>'''


def settings_xml():
    return f'''<?xml version="1.0" encoding="UTF-8" standalone="yes"?>
<w:settings xmlns:w="{NS_W}"><w:zoom w:percent="100"/><w:defaultTabStop w:val="720"/><w:updateFields w:val="true"/><w:compat/></w:settings>'''


def footer_xml():
    return f'''<?xml version="1.0" encoding="UTF-8" standalone="yes"?>
<w:ftr xmlns:w="{NS_W}" xmlns:r="{NS_R}"><w:p><w:pPr><w:jc w:val="center"/></w:pPr>
<w:r><w:rPr><w:color w:val="7F8C8D"/><w:sz w:val="18"/></w:rPr><w:t>Adaptive Verifiable ANN Tree  ·  </w:t></w:r>
<w:r><w:fldChar w:fldCharType="begin"/></w:r><w:r><w:instrText xml:space="preserve"> PAGE </w:instrText></w:r><w:r><w:fldChar w:fldCharType="end"/></w:r>
</w:p></w:ftr>'''


def content_types_xml():
    return '''<?xml version="1.0" encoding="UTF-8" standalone="yes"?>
<Types xmlns="http://schemas.openxmlformats.org/package/2006/content-types">
<Default Extension="rels" ContentType="application/vnd.openxmlformats-package.relationships+xml"/>
<Default Extension="xml" ContentType="application/xml"/>
<Default Extension="png" ContentType="image/png"/>
<Override PartName="/word/document.xml" ContentType="application/vnd.openxmlformats-officedocument.wordprocessingml.document.main+xml"/>
<Override PartName="/word/styles.xml" ContentType="application/vnd.openxmlformats-officedocument.wordprocessingml.styles+xml"/>
<Override PartName="/word/settings.xml" ContentType="application/vnd.openxmlformats-officedocument.wordprocessingml.settings+xml"/>
<Override PartName="/word/footer1.xml" ContentType="application/vnd.openxmlformats-officedocument.wordprocessingml.footer+xml"/>
<Override PartName="/docProps/core.xml" ContentType="application/vnd.openxmlformats-package.core-properties+xml"/>
<Override PartName="/docProps/app.xml" ContentType="application/vnd.openxmlformats-officedocument.extended-properties+xml"/>
</Types>'''


def root_rels_xml():
    return '''<?xml version="1.0" encoding="UTF-8" standalone="yes"?>
<Relationships xmlns="http://schemas.openxmlformats.org/package/2006/relationships">
<Relationship Id="rId1" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/officeDocument" Target="word/document.xml"/>
<Relationship Id="rId2" Type="http://schemas.openxmlformats.org/package/2006/relationships/metadata/core-properties" Target="docProps/core.xml"/>
<Relationship Id="rId3" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/extended-properties" Target="docProps/app.xml"/>
</Relationships>'''


def rels_xml(rels):
    items = []
    for rid, typ, target, mode in rels:
        mode_xml = f' TargetMode="{mode}"' if mode else ""
        items.append(f'<Relationship Id="{rid}" Type="{typ}" Target="{target}"{mode_xml}/>')
    return '<?xml version="1.0" encoding="UTF-8" standalone="yes"?>\n<Relationships xmlns="http://schemas.openxmlformats.org/package/2006/relationships">' + "".join(items) + "</Relationships>"


def core_xml():
    now = datetime.now(timezone.utc).replace(microsecond=0).isoformat().replace("+00:00", "Z")
    return f'''<?xml version="1.0" encoding="UTF-8" standalone="yes"?>
<cp:coreProperties xmlns:cp="http://schemas.openxmlformats.org/package/2006/metadata/core-properties" xmlns:dc="http://purl.org/dc/elements/1.1/" xmlns:dcterms="http://purl.org/dc/terms/" xmlns:dcmitype="http://purl.org/dc/dcmitype/" xmlns:xsi="http://www.w3.org/2001/XMLSchema-instance">
<dc:title>自适应可验证 ANN 树：索引构建与查询协议</dc:title><dc:subject>Proof-aware dendrogram and authenticated best-first search</dc:subject>
<dc:creator>Codex / ANN_project</dc:creator><cp:lastModifiedBy>Codex / ANN_project</cp:lastModifiedBy>
<dcterms:created xsi:type="dcterms:W3CDTF">{now}</dcterms:created><dcterms:modified xsi:type="dcterms:W3CDTF">{now}</dcterms:modified>
</cp:coreProperties>'''


def app_xml():
    return '''<?xml version="1.0" encoding="UTF-8" standalone="yes"?>
<Properties xmlns="http://schemas.openxmlformats.org/officeDocument/2006/extended-properties" xmlns:vt="http://schemas.openxmlformats.org/officeDocument/2006/docPropsVTypes">
<Application>Microsoft Office Word</Application><DocSecurity>0</DocSecurity><ScaleCrop>false</ScaleCrop><Company>ANN_project</Company><AppVersion>16.0000</AppVersion>
</Properties>'''


def add_title_and_summary(d: DocBuilder):
    d.paragraph("自适应可验证 ANN 树", style="Title")
    d.paragraph("Proof-Aware Dendrogram 索引构建与 Authenticated Best-First 查询协议", style="Subtitle")
    d.paragraph("技术设计说明 · 2026-08-31", align="center", color="5B6573", size=11, after=260)
    d.paragraph(
        "文档定位",
        bold=True, color="C0392B", size=12, border="C0392B", after=60,
    )
    d.paragraph(
        "本文完整描述的是下一版拟议算法。当前已经实验验证的是 balanced 1–3 层候选树与静态 probes；"
        "proof-aware agglomerative dendrogram、VO 动态剪枝、γ-best-first 查询和 ρ→γ Recall 校准尚未实现。"
        "文中的小型数值例子用于解释协议，不是实验测量结果。",
        color="7B241C", border="C0392B", after=180,
    )
    d.paragraph("阅读路线", style="Heading2")
    d.table(
        ["部分", "回答的问题"],
        [
            ["第 1–2 节", "统一目标是什么，索引如何从 micro-clusters 构建成可变深度认证树？"],
            ["第 3 节", "查询如何不依赖固定 pc/pm/pf，而由一个参数控制展开与停止？"],
            ["第 4–5 节", "完整建树例子和逐步查询例子如何运行？"],
            ["第 6–8 节", "目前有哪些真实实验支撑、哪些尚未验证、下一步如何实验？"],
        ], widths=[1700, 7300], font_size=9,
    )
    d.paragraph("核心结论", style="Heading2")
    d.bullet("索引构建的统一直觉：内部节点是一道认证过滤门；只有它隐藏的下层 VO 大于自身的 code、metadata、model 和 proof 成本时才保留。")
    d.bullet("查询的统一直觉：全树维护一个按可认证下界排序的 frontier；每次展开当前最有希望的节点，不再预设每层 top 数量。")
    d.bullet("单一控制参数可以是 γ∈[0,1]；对用户暴露目标 Recall ρ，系统离线校准 ρ→γ。γ=1 可退化为精确 branch-and-bound。")
    d.bullet("密码学能够保证服务器按承诺树、优先队列和停止规则执行；除 exact 模式外，Recall 是校准意义下的统计保证。")


def add_problem_and_model(d: DocBuilder):
    d.paragraph("1. 目标、符号与保证边界", style="Heading1")
    d.paragraph("1.1 目标", style="Heading2")
    d.paragraph(
        "给定向量集合 X={x₁,…,xₙ}、查询 q、返回数量 k 和目标 Recall ρ，构建一棵经密码学承诺的层次索引 T。"
        "目标不是单独最小化搜索距离计算，而是在 Recall 约束下最小化验证对象（VO）和查询时间。"
    )
    d.code("minimize    E[ VO(T, q, policy) ]\nsubject to  Recall@k >= ρ")
    d.paragraph(
        "VO 必须计入 root/child center codes、PQ/SQ 参数、选择 metadata、Merkle multiproofs、叶对象渐进证明及 framing。"
        "因此普通 ANN 中 CPU 友好的大分支节点，在 stateless verifier 模型下可能非常昂贵。"
    )
    d.paragraph("1.2 关键符号", style="Heading2")
    d.table(
        ["符号", "含义"],
        [
            ["T", "最终保留的认证树；可以不平衡、不同区域深度不同"],
            ["D", "过分割后得到的完整 dendrogram scaffold"],
            ["u", "内部节点或叶节点；children(u) 为其当前保留孩子"],
            ["ĉᵤ", "节点中心的解码值；通常由 retained parent center + residual code 重建"],
            ["Rᵤ", "覆盖该子树所有原始对象的安全半径"],
            ["eᵤ", "中心量化/重建误差的安全上界"],
            ["LB(q,u)", "查询到节点子树任意对象距离的安全下界"],
            ["τ", "当前已访问候选中第 k 小的精确距离"],
            ["γ", "查询停止强度；越大展开越多，γ=1 为 exact 模式"],
            ["ρ", "用户目标 Recall；由校准映射成 γρ"],
        ], widths=[1700, 7300], font_size=8.6,
    )
    d.paragraph("1.3 三种不同的保证", style="Heading2")
    d.table(
        ["保证", "性质", "可以声称什么"],
        [
            ["执行/完整性保证", "确定性、密码学", "服务器没有伪造节点、漏掉已承诺孩子或违反优先队列与停止规则"],
            ["距离近似保证", "确定性、几何", "安全下界成立时，停止条件给出第 k 距离的 1/γ 近似"],
            ["Recall@k 保证", "统计性", "未来查询与 calibration queries 可交换时，以固定置信度达到目标平均 Recall"],
            ["Exact 模式", "确定性", "γ=1 且安全半径有效时返回全局精确 top-k，但最坏可能近似全扫描"],
        ], widths=[1800, 1800, 5400], font_size=8.5,
    )


def add_index_algorithm(d: DocBuilder, figures):
    d.paragraph("2. 自适应索引构建算法", style="Heading1")
    d.image(figures["construction"], 6.7, "图 1　统一的 proof-aware 索引构建流水线。图中文字使用英文以保证跨平台字体兼容。")
    d.paragraph("2.1 输入与输出", style="Heading2")
    d.table(
        ["输入", "说明"],
        [
            ["数据 X", "原始向量、对象 ID 与对象编码格式"],
            ["B_leaf", "叶认证块允许的最大字节数；属于存储系统参数，不是语义簇数量"],
            ["Codec family", "例如 root SQ4、内部 residual PQ24、近叶 residual PQ16"],
            ["ρ-grid", "索引希望支持的 Recall 工作点，例如 {0.90,0.95,0.99}"],
            ["Hash/commitment", "SHA-256、Merkle layout、确定性 tie-breaking"],
        ], widths=[2000, 7000], font_size=8.8,
    )
    d.paragraph(
        "输出是最终认证树 T、每个节点的 descriptor/commitment、安全半径与 residual code，以及 Recall 目标到停止参数的校准表 ρ→γρ。"
    )
    d.paragraph("2.2 第一步：构建 atomic micro-clusters", style="Heading2")
    d.paragraph(
        "micro-cluster 不直接等于最终 fine leaf。它是最小物理认证块：继续切分只为了保证单个叶对象块可被传输、验证和缓存。"
        "从根开始对超出 B_leaf 的块做 balanced 2-way 或 4-way split，直到每个块的编码字节数不超过上限，或块内方差已经低于数值阈值。"
    )
    d.code(
        "MICROCLUSTER(X, B_leaf):\n"
        "  queue <- {X}\n"
        "  while exists block L with EncodedBytes(L) > B_leaf:\n"
        "      split L by balanced k-means (b=2 or 4)\n"
        "      replace L by its children\n"
        "  return atomic blocks M0...Mm-1"
    )
    d.paragraph(
        "这样 micro-cluster 数量由 N、维数、对象编码长度和块页大小共同产生，而不是预先指定 K=65536。"
        "最终有效叶数量稍后由 VO pruning 决定；多个 atomic blocks 可以在树中被同一道 gate 管理。"
    )
    d.paragraph("2.3 节点 descriptor", style="Heading2")
    d.table(
        ["字段", "用途", "是否承诺"],
        [
            ["node_id / level", "确定性定位与 transcript replay", "是"],
            ["residual_code", "从 retained parent 重建中心", "是"],
            ["radius Rᵤ", "形成安全 lower bound", "是"],
            ["decode error eᵤ", "补偿 SQ/PQ 重建误差", "是"],
            ["subtree_count / byte_count", "构建成本模型和防止结构省略", "是"],
            ["child_count / child_root", "认证完整 child block", "是"],
            ["leaf_object_root", "叶对象完整性和渐进证明", "叶节点"],
        ], widths=[2200, 4700, 2100], font_size=8.4,
    )
    d.paragraph("2.4 第二步：proof-aware 自底向上合并", style="Heading2")
    d.paragraph(
        "为避免 O(m²) 全对比较，先在 micro-cluster centers 上构建一个稀疏近邻图；只允许图上相邻且满足容量约束的节点成为 merge candidates。"
        "合并 a、b 后父中心采用对象数加权中心，并计算新的安全半径。候选优先级同时考虑三个部分："
    )
    d.code(
        "MergeScore(a,b) = GeoByteEquivalent(a,b)\n"
        "                + ResidualCodeBytes(a,b | parent)\n"
        "                + AuthenticationBytes(parent, children)"
    )
    d.bullet("GeoByteEquivalent：可采用 Ward linkage 增量失真 nₐnᵦ/(nₐ+nᵦ)·||cₐ−cᵦ||²，再通过离线 λD 换算为字节惩罚。")
    d.bullet("ResidualCodeBytes：实际编码孩子 residual 后得到的码长和 codec model 分摊，而不是只用欧氏距离。")
    d.bullet("AuthenticationBytes：node header、child descriptors、Merkle root/multiproof 和 framing。")
    d.bullet("容量平衡应按 AuthBytes 平衡，而不只是孩子数量平衡，即 byte-balanced clustering。")
    d.paragraph(
        "λD 不应由某个数据集的手调经验固定：可以生成若干 λD 候选 dendrogram，最终统一由完整 VO+Recall 目标选择。"
        "因此合并阶段负责产生结构候选，pruning 阶段负责做最终系统决策。"
    )
    d.paragraph("2.5 第三步：形成完整 dendrogram", style="Heading2")
    d.paragraph(
        "重复取 MergeScore 最小的合法 merge，更新近邻图和父节点 descriptor，直到只剩一个根。"
        "完整 dendrogram D 是多分辨率 scaffold，而不是最终查询树；它可以是二叉树，稍后的路径压缩会产生实际多叉树。"
    )
    d.paragraph("2.6 第四步：基于认证门收益做动态剪枝", style="Heading2")
    d.paragraph(
        "对每个内部节点 u 比较两种选择：Keep 让 u 成为一道可查询 gate；Collapse 删除 u，让最近 retained ancestor 直接连接 u 的保留后代。"
        "Collapse 后必须相对新的 retained parent 重新编码 residual。"
    )
    d.code(
        "KeepGain(u, ρ) = ExpectedVO_if_collapsed(u, ρ)\n"
        "               - ExpectedVO_if_kept(u, ρ)\n\n"
        "Keep u      if KeepGain(u,ρ) > 0\n"
        "Collapse u  otherwise"
    )
    d.paragraph(
        "ExpectedVO 使用第 3 节同一 best-first 查询协议估计。小型独立 calibration set 只用于 Recall 可行性和展开概率，"
        "不使用热门查询频率或生产 workload 日志。若要求一棵树支持多个 ρ，可最小化 Recall–VO 曲线的加权面积："
    )
    d.code("Objective(T) = Σρ wρ · VO(T, γρ)     subject to LCB(Recallρ) >= ρ")
    d.paragraph(
        "动态规划从叶向根计算 Keep/Collapse；因为 Collapse 会改变 residual parent，工程实现可先使用估计码长做 DP，"
        "再对最终保留边重编码并进行一次局部修正。"
    )
    d.paragraph("2.7 第五步：重编码并建立 commitment", style="Heading2")
    d.bullet("对所有 retained edge 重新计算 child center − retained parent center。")
    d.bullet("训练或选择每层 codec；对固定宽度与 canonical Huffman 直接取真实总字节较小者。")
    d.bullet("重新计算覆盖原始对象的 Rᵤ，并把中心量化误差并入 eᵤ；不可只覆盖 child centers。")
    d.bullet("每个内部节点对完整 child descriptor block 建 Merkle commitment；根 commitment 由数据拥有者签名。")
    d.bullet("每个叶节点建立对象 commitment 和 progressive distance-certificate 参数。")
    d.paragraph("2.8 算法 1：完整伪代码", style="Heading2")
    d.code(
        "BUILD-PROOF-AWARE-TREE(X, B_leaf, codecs, rho_grid):\n"
        "  M <- MICROCLUSTER(X, B_leaf)\n"
        "  G <- sparse kNN graph over centers(M)\n"
        "  D <- leaves M\n"
        "  while D has more than one root:\n"
        "      choose legal neighboring group with minimum MergeScore\n"
        "      create parent, radius, residual-size and auth-size estimates\n"
        "      update G and D\n"
        "  for rho in rho_grid:\n"
        "      estimate best-first traces and calibrate gamma_rho\n"
        "  T <- bottom-up DP choosing Keep versus Collapse\n"
        "  recompute retained-parent residuals, safe radii and codec models\n"
        "  commit all child blocks and leaf object blocks\n"
        "  return T, signed_root, calibration_table"
    )
    d.paragraph("2.9 复杂度与工程实现", style="Heading2")
    d.table(
        ["阶段", "建议实现", "典型复杂度/风险"],
        [
            ["Micro-clustering", "balanced recursive split", "约 O(N log m)，取决于 k-means 迭代"],
            ["Merge candidates", "center kNN graph，只看 s 个近邻", "O(ms log m)，避免 O(m²)"],
            ["Agglomeration", "heap + lazy invalidation", "约 O(ms log m)"],
            ["VO pruning", "bottom-up DP + 少量重编码修正", "O(|D|·|ρ-grid|) 加 calibration traces"],
            ["Commitment", "局部 Merkle trees，可并行", "对总 descriptor/object bytes 线性"],
        ], widths=[1900, 3300, 3800], font_size=8.4,
    )


def add_query_protocol(d: DocBuilder, figures):
    d.paragraph("3. Authenticated Best-First 查询协议", style="Heading1")
    d.image(figures["query_protocol"], 5.8, "图 2　认证 best-first 查询状态机。服务器与验证者维护同一个 frontier。")
    d.paragraph("3.1 安全下界与优先级", style="Heading2")
    d.paragraph(
        "对节点 u，使用解码中心 ĉᵤ、安全半径 Rᵤ 和量化误差 eᵤ："
    )
    d.code("LB(q,u) = max(0, ||q - ĉᵤ|| - Rᵤ - eᵤ)")
    d.paragraph(
        "如果半径覆盖子树所有原始对象，则任意 x∈subtree(u) 都满足 d(q,x)≥LB(q,u)。"
        "全局 frontier 按 (LB,node_id) 排序；node_id 提供确定性 tie-break。"
    )
    d.paragraph("3.2 查询流程", style="Heading2")
    d.code(
        "AUTH-BEST-FIRST(q, k, gamma):\n"
        "  authenticate root child descriptor block\n"
        "  frontier <- all root children keyed by (LB(q,u), node_id)\n"
        "  topk <- empty; tau <- infinity\n"
        "  repeat:\n"
        "      if |topk| >= k and minLB(frontier) >= gamma * tau: break\n"
        "      u <- pop minimum frontier node\n"
        "      if u is internal:\n"
        "          reveal and authenticate complete child descriptor block\n"
        "          reconstruct children and push them into frontier\n"
        "      else:\n"
        "          verify leaf object block/progressive certificates\n"
        "          compute exact distances and update topk, tau\n"
        "  return topk, transcript, terminal frontier descriptors"
    )
    d.paragraph("3.3 为什么展开父节点时返回完整 child block", style="Heading2")
    d.paragraph(
        "验证者必须知道父节点是否隐藏了一个更优孩子。最简单可靠的方式是返回该父节点完整的 child descriptors，"
        "让验证者重算所有 LB 并重建局部 Merkle root。只返回 top children 需要额外排序证明、向量承诺或零知识证明，"
        "会显著增加协议复杂度。树的 fanout 因而直接影响 VO。"
    )
    d.paragraph("3.4 单参数停止规则", style="Heading2")
    d.code("STOP  iff  |topk| >= k  and  minLB(frontier) >= gamma * tau")
    d.table(
        ["γ", "行为", "保证/代价"],
        [
            ["0", "获得 k 个候选即可停止", "VO 最小，Recall 可能较低"],
            ["0<γ<1", "按 frontier gap 自适应展开", "确定性 1/γ 距离近似；Recall 需校准"],
            ["1", "所有未访问子树下界均不可能击败 τ", "精确 top-k；高维最坏可能接近全扫描"],
        ], widths=[1000, 3600, 4400], font_size=8.7,
    )
    d.paragraph(
        "由停止条件可得所有未访问对象 d(q,x)≥γτ，因此返回的第 k 距离满足 τ≤d*k/γ。"
        "该距离近似保证并不等价于 Recall：大量近距离并列对象可能在很小距离误差下互相替换。"
    )
    d.paragraph("3.5 用户只提供目标 Recall ρ", style="Heading2")
    d.paragraph(
        "系统固定置信水平 δ（例如 0.01 或 0.05），用户只提供 ρ。使用独立 calibration queries 对自动生成的 γ 序列运行查询，"
        "选择满足 Recall 置信下界的最小 γ："
    )
    d.code(
        "gamma_rho = argmin_gamma  VO(gamma)\n"
        "            s.t. LCB_delta( mean Recall@k(gamma) ) >= rho"
    )
    d.paragraph(
        "γ 越大时搜索集合通过累积展开而增长；实现应保留已经访问的候选，保证 effort 单调。"
        "对于 0.99 级别结论，应以 query 为独立统计单位并使用数百至数千条 calibration queries；当前原型的 60 条仅适合调试。"
    )
    d.paragraph("3.6 验证者重放规则", style="Heading2")
    d.table(
        ["验证步骤", "检查内容"],
        [
            ["Root", "签名 root、root child count、完整 root descriptor block"],
            ["Expansion", "当前节点确实是 (LB,node_id) 最小 frontier 元素"],
            ["Child block", "child count、descriptor bytes、局部 Merkle root 与父 descriptor 一致"],
            ["Leaf", "对象 commitment、渐进证书、精确 refinement 和 top-k 更新一致"],
            ["Termination", "|topk|≥k 且 terminal minLB≥γτ"],
            ["Output", "结果 ID、距离和 transcript commitment 一致"],
        ], widths=[1800, 7200], font_size=8.6,
    )
    d.paragraph("3.7 每层 top 数量如何产生", style="Heading2")
    d.paragraph(
        "协议中没有静态 pc、pm、pf。对于查询 q 和 γ，各层实际展开数定义为 pℓ(q,γ)。"
        "容易查询可能只访问少量浅层 gate 和两个叶；边界查询会自动扩展更多兄弟，甚至在多个深度之间交错。"
        "这正是全局 best-first 相比逐层固定 beam 的主要优势。"
    )


def add_index_example(d: DocBuilder, figures):
    d.paragraph("4. 索引构建例子：8 个 micro-clusters", style="Heading1")
    d.paragraph(
        "本节使用二维示意数据解释算法。所有 bytes 和 KeepGain 均为教学数值，不代表 SIFT 实测。"
        "假设 32 个对象被物理块约束切成 8 个 atomic micro-clusters，每簇 4 个对象。"
    )
    d.paragraph("4.1 Atomic blocks", style="Heading2")
    d.table(
        ["Micro", "中心", "对象数", "对象块", "最近结构邻居"],
        [
            ["M0", "(0.8, 1.0)", "4", "384 B", "M1"],
            ["M1", "(1.7, 1.2)", "4", "384 B", "M0, M2"],
            ["M2", "(3.8, 0.9)", "4", "384 B", "M3, M1"],
            ["M3", "(4.6, 1.4)", "4", "384 B", "M2"],
            ["M4", "(8.1, 7.9)", "4", "384 B", "M5"],
            ["M5", "(8.8, 8.2)", "4", "384 B", "M4, M6"],
            ["M6", "(11.0, 8.0)", "4", "384 B", "M7, M5"],
            ["M7", "(12.1, 8.5)", "4", "384 B", "M6"],
        ], widths=[1000, 1700, 1200, 1500, 3600], font_size=8.4,
    )
    d.paragraph("4.2 合并候选", style="Heading2")
    d.table(
        ["候选 merge", "几何字节等价", "Residual bytes", "Auth bytes", "MergeScore"],
        [
            ["M4 + M5 → G2", "28", "50", "64", "142"],
            ["M2 + M3 → G1", "36", "54", "64", "154"],
            ["M0 + M1 → G0", "42", "54", "64", "160"],
            ["M6 + M7 → G3", "51", "55", "64", "170"],
            ["M1 + M2", "210", "60", "64", "334"],
        ], widths=[2300, 1800, 1700, 1500, 1700], font_size=8.5,
    )
    d.paragraph(
        "算法先完成四个局部 merge，再合并 G0/G1→P0、G2/G3→P1，最后 P0/P1→Root，得到完整 dendrogram。"
    )
    d.image(figures["dendrogram"], 6.8, "图 3　完整 dendrogram 与 VO pruning 后的非均匀有效树。")
    d.paragraph("4.3 认证门剪枝", style="Heading2")
    d.table(
        ["节点", "保留 gate 的预计 VO", "压缩 gate 的预计 VO", "KeepGain", "决策"],
        [
            ["P0", "1,420 B", "1,940 B", "+520 B", "Keep"],
            ["G0", "620 B", "880 B", "+260 B", "Keep"],
            ["G1", "735 B", "690 B", "−45 B", "Collapse"],
            ["P1", "1,510 B", "1,920 B", "+410 B", "Keep"],
            ["G2", "645 B", "840 B", "+195 B", "Keep"],
            ["G3", "720 B", "690 B", "−30 B", "Collapse"],
        ], widths=[1000, 2200, 2200, 1600, 2000], font_size=8.4,
    )
    d.paragraph(
        "G1 和 G3 被删除：P0 直接连接 M2/M3，P1 直接连接 M6/M7；G0 和 G2 被保留。"
        "因此树的不同区域自然具有不同深度，而不是人为指定统一两层或三层。"
    )
    d.paragraph("4.4 重编码和 commitment", style="Heading2")
    d.bullet("M2、M3 residual 改为相对 P0 编码；M6、M7 residual 改为相对 P1 编码。")
    d.bullet("P0 child block 为 {G0,M2,M3}；P1 child block 为 {G2,M6,M7}。")
    d.bullet("G0/G2 各自承诺两个 micro leaves；每个 micro leaf 承诺 4 个对象。")
    d.bullet("Root descriptor block 包含 P0/P1 的 center code、radius、error、count 与 child root。")


def add_query_example(d: DocBuilder, figures):
    d.paragraph("5. 查询例子：无固定 pc/pm/pf 的动态展开", style="Heading1")
    d.paragraph(
        "查询 q=(1.2,1.0)，k=3。假设用户请求目标 Recall ρ=0.99，校准表映射到 γρ=0.88。"
        "γ=0.88 仅为示例；真实映射必须通过实验获得。"
    )
    d.image(figures["query_example"], 6.8, "图 4　查询只展开 P0→G0→M0/M1；其他节点作为已认证 frontier 保留而不展开。")
    d.paragraph("5.1 Transcript 逐步状态", style="Heading2")
    d.table(
        ["步", "Frontier 最小项", "动作", "τ", "停止检查"],
        [
            ["0", "P0, LB=0.00", "认证 Root block；P0/P1 入队", "∞", "候选不足"],
            ["1", "P0, LB=0.00", "展开 P0；加入 G0、M2、M3", "∞", "候选不足"],
            ["2", "G0, LB=0.05", "展开 G0；加入 M0、M1", "∞", "候选不足"],
            ["3", "M0, LB=0.10", "验证并扫描 M0 的 4 个对象", "1.15", "0.55 < 0.88×1.15，继续"],
            ["4", "M1, LB=0.55", "验证并扫描 M1 的 4 个对象", "0.78", "2.10 ≥ 0.88×0.78，停止"],
        ], widths=[700, 2000, 3300, 1000, 3000], font_size=8.2,
    )
    d.paragraph("5.2 验证者收到的材料", style="Heading2")
    d.table(
        ["材料", "作用", "示意字节"],
        [
            ["Root descriptor block", "重建 P0/P1 并验证 root commitment", "192 B"],
            ["P0 child block", "证明 G0/M2/M3 完整并计算其 LB", "288 B"],
            ["G0 child block", "证明 M0/M1 完整并计算其 LB", "160 B"],
            ["Merkle/framing metadata", "连接各局部 commitment 与 transcript", "384 B"],
            ["M0/M1 leaf VO", "对象、渐进证书、局部 proofs", "960 B"],
            ["公共参数分摊", "codec/radius format/version", "256 B"],
            ["总计", "教学示意，不是实测", "2,240 B"],
        ], widths=[2600, 4800, 1600], font_size=8.4,
    )
    d.paragraph(
        "如果 flat 认证方案需要为同一查询传输全部 micro routing material 和更多叶对象，假设为 6,656 B，"
        "则该示意查询节省约 66.3%。真实收益取决于高维半径是否紧、child blocks 是否足够小，以及 γ 对 Recall 的校准结果。"
    )
    d.paragraph("5.3 为什么这是 query-adaptive", style="Heading2")
    d.paragraph(
        "该查询在第一级展开 1 个节点、第二级展开 1 个 gate、最终扫描 2 个 micro leaves。"
        "另一条位于 P0/P1 边界的查询可能交错展开两个大分支并扫描更多 leaves。"
        "每层访问数是 transcript 的输出，而不是索引配置中的固定常量。"
    )


def add_status_and_plan(d: DocBuilder):
    d.paragraph("6. 与当前实验结果的关系", style="Heading1")
    d.paragraph("6.1 已经实际验证的简化系统", style="Heading2")
    d.table(
        ["数据", "已验证结构", "静态 probes", "Recall@100", "完整总 VO", "ANN 延迟"],
        [
            ["SIFT50K", "512→32768", "102→721", "0.99105", "395.85 KiB", "2.291 ms"],
            ["SIFT100K", "512→65536", "102→786", "0.99035", "569.28 KiB", "2.695 ms"],
            ["SIFT200K", "512→65536", "133→1442", "0.99047", "974.06 KiB", "4.384 ms"],
            ["HierGauss64-50K", "64→1024→16384", "5→32→100", "0.99127", "60.98 KiB", "0.171 ms"],
        ], widths=[1800, 2000, 1600, 1400, 1400, 1400], font_size=8.1,
    )
    d.paragraph(
        "这些数字来自 balanced 1–3 层候选搜索、静态 probe tuple 和完整 progressive leaf VO。"
        "SIFT100K 自动方案相对同预算手调点的最佳编码 VO 高 4.06%，但 ANN 延迟低 26.89%；所有 violations=0。"
    )
    d.paragraph("6.2 尚未验证的完整方案", style="Heading2")
    d.table(
        ["组件", "当前状态", "主要未知量"],
        [
            ["Proof-aware agglomerative merge", "未实现", "MergeScore 是否产生更好的可认证局部性"],
            ["Keep/Collapse VO dynamic programming", "未实现", "估计成本与最终重编码成本的偏差"],
            ["安全 radius/error lower bound", "未实验", "SIFT128 高维下界是否过松"],
            ["γ-best-first transcript", "未实现", "相同 Recall 下 VO/latency 是否优于静态 probes"],
            ["ρ→γ 置信校准", "未实现", "需要多少 calibration queries 才能支撑 0.99"],
            ["非均匀树 commitment", "未实现", "路径压缩后 residual/model/proof 的工程成本"],
        ], widths=[2700, 1500, 4800], font_size=8.4,
    )
    d.paragraph("因此目前不能把第 2–5 节的示意 VO 和延迟当成实验结论。", bold=True, color="C0392B")

    d.paragraph("7. 推荐的隔离实验顺序", style="Heading1")
    d.table(
        ["阶段", "保持不变", "新增内容", "成功标准"],
        [
            ["A：查询协议", "当前 2L/3L balanced tree", "radius/error + γ-best-first + transcript", "Recall=0.99 时 VO 不劣于静态 probes 10%，且部分查询明显更省"],
            ["B：树剪枝", "阶段 A 查询协议", "在现有层次树上做 Keep/Collapse", "VO 进一步下降且 Recall/latency 不退化"],
            ["C：新建树", "阶段 A/B verifier", "proof-aware agglomeration + byte balance", "跨 SIFT/GIST/Deep/Glove 稳定优于 HKM baseline"],
            ["D：统计保证", "最终结构和协议", "LCB/calibration、多随机种子", "报告置信区间、失败率和 distribution shift"],
        ], widths=[1500, 2300, 2600, 3300], font_size=8.1,
    )
    d.paragraph("阶段 A 是最关键的 go/no-go 检查。", bold=True)
    d.paragraph(
        "如果安全半径在高维中过松，γ-best-first 会展开大量节点；此时不应立即实现复杂 dendrogram。"
        "可以退回近似 score + 统计 Recall 停止，或设计更紧的 per-dimension box/PQ lower bound。"
    )
    d.paragraph("建议记录的指标", style="Heading2")
    d.bullet("Recall–VO、Recall–ANN latency、Recall–verification latency 三条完整曲线，而不只报告单点。")
    d.bullet("每查询和每层的 expanded nodes、revealed descriptor bytes、Merkle siblings、visited objects。")
    d.bullet("LB=0 的 frontier 比例、Rᵤ/||q−ĉᵤ|| 分布，用于诊断高维 bound quality。")
    d.bullet("proxy VO 与完整协议 VO 的误差，尤其是高叶密度和路径压缩场景。")
    d.bullet("index build time、peak memory、树节点数、平均/最大 fanout 和 depth distribution。")


def add_risks_and_refs(d: DocBuilder):
    d.paragraph("8. 设计风险与可选改进", style="Heading1")
    d.table(
        ["风险", "表现", "可选改进"],
        [
            ["高维安全球过松", "大量 LB=0，γ 接近 1 时近似全扫描", "per-dimension boxes、PQ subspace bounds、多个 pivots；保持安全误差项"],
            ["节点 fanout 过大", "一次展开 child block VO 过大", "byte-balanced capacity、fanout-aware MergeScore"],
            ["树过深", "metadata/proof/model 层数抵消过滤收益", "Keep/Collapse DP、path compression、共享 codec models"],
            ["Calibration 过小", "ρ=0.99 在测试集失效", "query-level LCB、扩大 calibration、固定独立 test"],
            ["Distribution shift", "ρ→γ 映射过时", "监控匿名难度统计、保守 exact fallback；不修改已签名结构"],
            ["Merge 只看中心距离", "几何近但不可共同过滤", "加入 residual bytes、radius growth 和认证 gate cost"],
        ], widths=[1900, 3500, 3600], font_size=8.3,
    )
    d.paragraph("8.1 可以复用的已有思想", style="Heading2")
    d.bullet("Vocabulary Tree：递归 k-means 与层次量化，但原目标不是认证 VO。")
    d.bullet("FLANN HKM priority search：best-first traversal 和自动 precision 配置，是查询算法的重要先例。")
    d.bullet("IVFADC/PQ/Residual Quantization：中心 residual 编码和 asymmetric distance computation。")
    d.bullet("HNSW/ANNProof：图层次搜索和 Merkle 化搜索路径，但其 frontier 与树 child-block 证明成本不同。")
    d.bullet("MR-tree/VSS-tree：认证空间层次索引和几何 lower bound；高维场景需要重新检验 bound quality。")
    d.paragraph("8.2 建议论文贡献表述", style="Heading2")
    d.paragraph(
        "不应把贡献表述成“提出一种新的层次 k-means 树”。更准确的定位是："
    )
    d.paragraph(
        "提出 authentication-aware rate–distortion objective，将 atomic leaf layout、兄弟合并、residual 编码、"
        "fanout/depth pruning 与单参数 authenticated best-first stopping 统一到同一个 VO–Recall 目标。",
        bold=True, color="1F4E79", border="2E75B6",
    )

    d.paragraph("9. 参考文献", style="Heading1")
    refs = [
        "[1] D. Nistér and H. Stewénius. Scalable Recognition with a Vocabulary Tree. CVPR 2006. https://research.google/pubs/scalable-recognition-with-a-vocabulary-tree/",
        "[2] M. Muja and D. G. Lowe. Fast Approximate Nearest Neighbors with Automatic Algorithm Configuration. VISAPP 2009. https://www.cs.ubc.ca/~lowe/papers/09muja.pdf",
        "[3] H. Jégou, M. Douze, and C. Schmid. Product Quantization for Nearest Neighbor Search. TPAMI 2011. https://doi.org/10.1109/TPAMI.2010.57",
        "[4] A. Babenko and V. Lempitsky. The Inverted Multi-Index. CVPR 2012. https://www.robots.ox.ac.uk/~vilem/cvpr2012.pdf",
        "[5] Y. A. Malkov and D. A. Yashunin. Efficient and Robust Approximate Nearest Neighbor Search Using HNSW Graphs. TPAMI. https://arxiv.org/abs/1603.09320",
        "[6] L. Lu et al. ANNProof: Building a Verifiable and Efficient Outsourced Approximate Nearest Neighbor Search System on Blockchain. Future Generation Computer Systems, 2024. https://www.sciencedirect.com/science/article/pii/S0167739X24000785",
        "[7] J. Hong, T. Wen, and Q. Guo. Query Authentication of Outsourced Spatial Database (VSS-tree). IAJIT 2020. https://doi.org/10.34028/iajit/17/4/12",
        "[8] Local experiment report: VeriANN/AUTO_VO_TREE_RESULTS_20260830.md.",
    ]
    for ref in refs:
        d.paragraph(ref, size=9, after=90)
    d.paragraph("附录：实现时必须固定的确定性规则", style="Heading2")
    d.bullet("浮点距离比较的精度、相等距离时的 node_id/object_id tie-break。")
    d.bullet("Codec 参数、中心重建方式、radius/error 的上取整和序列化格式。")
    d.bullet("Child descriptor 的排序、Merkle padding、dummy node 与 multiproof 编码。")
    d.bullet("VO byte budget 的计数边界、framing、模型头部和公共参数是否按查询传输。")
    d.bullet("Calibration/test 切分、随机种子、置信水平和 ρ→γ 表的版本哈希。")


def build_docx(figures):
    d = DocBuilder()
    add_title_and_summary(d)
    add_problem_and_model(d)
    add_index_algorithm(d, figures)
    add_query_protocol(d, figures)
    add_index_example(d, figures)
    add_query_example(d, figures)
    add_status_and_plan(d)
    add_risks_and_refs(d)

    with zipfile.ZipFile(OUT, "w", compression=zipfile.ZIP_DEFLATED) as z:
        z.writestr("[Content_Types].xml", content_types_xml())
        z.writestr("_rels/.rels", root_rels_xml())
        z.writestr("word/document.xml", d.build_document_xml())
        z.writestr("word/styles.xml", styles_xml())
        z.writestr("word/settings.xml", settings_xml())
        z.writestr("word/footer1.xml", footer_xml())
        z.writestr("word/_rels/document.xml.rels", rels_xml(d.doc_rels))
        z.writestr("docProps/core.xml", core_xml())
        z.writestr("docProps/app.xml", app_xml())
        for name, path in d.images:
            z.write(path, f"word/media/{name}")


def main():
    ASSET_DIR.mkdir(parents=True, exist_ok=True)
    figures = {
        "construction": ASSET_DIR / "01_construction_pipeline.png",
        "dendrogram": ASSET_DIR / "02_full_vs_pruned_tree.png",
        "query_protocol": ASSET_DIR / "03_query_protocol.png",
        "query_example": ASSET_DIR / "04_query_example.png",
    }
    fig_construction(figures["construction"])
    fig_dendrogram(figures["dendrogram"])
    fig_query_protocol(figures["query_protocol"])
    fig_query_example(figures["query_example"])
    build_docx(figures)
    print(f"generated {OUT}")
    print(f"size {OUT.stat().st_size} bytes")


if __name__ == "__main__":
    main()
