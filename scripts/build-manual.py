#!/usr/bin/env python3
"""Render the tracked manual PDF with PyQt6 (host tool, never runs on tablet).

Install PyQt6 in a host virtual environment, then run this script from it.
The Markdown source controls page breaks with horizontal rules. Font embedding
and real PDF text keep the document searchable and readable in My files.
"""
import os
from pathlib import Path
import sys

os.environ.setdefault("QT_QPA_PLATFORM", "offscreen")
from PyQt6.QtCore import QMarginsF, QRectF, QSizeF, Qt
from PyQt6.QtGui import QColor, QFont, QGuiApplication, QImage, QPainter, QPageSize, QPdfWriter, QTextCharFormat, QTextCursor, QTextDocument

ROOT = Path(__file__).resolve().parent.parent
app = QGuiApplication(sys.argv)
output = ROOT / "docs/Inkline Manual.pdf"
writer = QPdfWriter(str(output))
writer.setResolution(144)
writer.setPageSize(QPageSize(QSizeF(157, 210), QPageSize.Unit.Millimeter, "Inkline tablet"))
writer.setPageMargins(QMarginsF(12, 12, 12, 12))
writer.setTitle("Inkline user guide — 0.3.6")
writer.setCreator("Inkline")
painter = QPainter(writer)
width, height = writer.width(), writer.height()
scale = writer.resolution() / 72
body_height = height / scale - 42
pages = (ROOT / "docs/manual.md").read_text().split("\n---\n")
logo = QImage(str(ROOT / "assets/goblin.png")).scaled(100, 100, Qt.AspectRatioMode.KeepAspectRatio, Qt.TransformationMode.SmoothTransformation)
page_number = 0
for section in pages:
    document = QTextDocument()
    font = QFont("Helvetica", 9)
    document.setDefaultFont(font)
    document.setDefaultStyleSheet("table { border-collapse: collapse; } td, th { padding: 4px; } code { font-family: monospace; } a { color: black; }")
    document.setMarkdown(section.strip())
    # Markdown's default link color ignores the HTML stylesheet. Keep all
    # text, including live hyperlinks, solid black on the e-paper document.
    cursor = QTextCursor(document)
    cursor.select(QTextCursor.SelectionType.Document)
    ink = QTextCharFormat(); ink.setForeground(QColor("black"))
    cursor.mergeCharFormat(ink)
    document.setPageSize(QSizeF(width / scale, body_height))
    for part in range(document.pageCount()):
        if page_number:
            writer.newPage()
        page_number += 1
        painter.save()
        painter.scale(scale, scale)
        painter.setPen(QColor("black"))
        painter.setFont(QFont("Helvetica", 8))
        painter.drawText(QRectF(0, 0, width / scale, 18), "INKLINE  /  USER GUIDE  /  0.3.6")
        painter.drawImage(QRectF(width / scale - 20, 0, 20, 20), logo)
        painter.translate(0, 24)
        painter.setClipRect(QRectF(0, 0, width / scale, body_height))
        painter.translate(0, -part * body_height)
        document.drawContents(painter, QRectF(0, part * body_height, width / scale, body_height))
        painter.restore()
        painter.save()
        painter.scale(scale, scale)
        painter.setFont(QFont("Helvetica", 8))
        painter.drawText(QRectF(0, height / scale - 12, width / scale, 12), Qt.AlignmentFlag.AlignLeft, "inkline.goblinreactor.com")
        painter.drawText(QRectF(0, height / scale - 12, width / scale, 12), Qt.AlignmentFlag.AlignRight, str(page_number))
        painter.restore()
painter.end()
print(f"{output}: {page_number} pages, {output.stat().st_size:,} bytes")
