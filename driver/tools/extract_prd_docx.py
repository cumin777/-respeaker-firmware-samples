import zipfile
from xml.etree import ElementTree as ET
from pathlib import Path

DOCX = Path(r"C:\ncs\v3.2.1\l7_e1\document\reSpeaker穿戴式AI麦克风产品需求书.docx")
OUT = Path(r"C:\ncs\v3.2.1\l7_e1\document\reSpeaker穿戴式AI麦克风产品需求书.extracted.txt")
NS = {"w": "http://schemas.openxmlformats.org/wordprocessingml/2006/main"}


def main() -> None:
    if not DOCX.exists():
        raise SystemExit(f"DOCX not found: {DOCX}")

    with zipfile.ZipFile(DOCX) as z:
        xml = z.read("word/document.xml")

    root = ET.fromstring(xml)

    lines: list[str] = []
    for p in root.findall(".//w:p", NS):
        parts: list[str] = []
        for t in p.findall(".//w:t", NS):
            parts.append(t.text or "")
        line = "".join(parts).strip()
        if line:
            lines.append(line)

    OUT.write_text("\n".join(lines), encoding="utf-8")
    print(f"extracted_lines={len(lines)}")
    print(f"wrote={OUT} bytes={OUT.stat().st_size}")


if __name__ == "__main__":
    main()
