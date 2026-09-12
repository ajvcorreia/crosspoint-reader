#!/usr/bin/env python3
"""
Phase 0 EPUB generator spike: produce STORED-only (uncompressed) EPUB.

This script generates a minimal, spec-valid EPUB with all zip entries using
ZIP_STORED (no compression). Purpose: validate that on-device zip writers
capable of only emitting uncompressed entries can still produce readable EPUBs.

The structure (mimetype, META-INF/container.xml, OEBPS/content.opf,
OEBPS/nav.xhtml, chapters, images) is adapted from the proven
scripts/generate_test_epub.py templates.
"""

import sys
import zipfile
from pathlib import Path

try:
    from PIL import Image, ImageDraw
except ImportError:
    print("Please install Pillow: pip install Pillow")
    sys.exit(1)

OUTPUT_DIR = Path(__file__).parent.parent / "test" / "epubs"


def create_solid_test_image(width=200, height=200, gray_value=128):
    """
    Create a minimal solid-color grayscale JPEG for testing.
    """
    img = Image.new('L', (width, height), gray_value)
    return img


def create_stored_epub(epub_path):
    """
    Create a minimal EPUB with all entries stored (not compressed).

    - One chapter (XHTML)
    - One embedded JPEG image
    - Standard EPUB structure: mimetype, META-INF/container.xml,
      OEBPS/content.opf, OEBPS/nav.xhtml
    """
    with zipfile.ZipFile(epub_path, 'w', zipfile.ZIP_STORED) as epub:
        # CRITICAL: mimetype MUST be first, uncompressed, no extra field
        epub.writestr('mimetype', 'application/epub+zip', compress_type=zipfile.ZIP_STORED)

        # Container (standard EPUB 3.0)
        container_xml = '''<?xml version="1.0" encoding="UTF-8"?>
<container version="1.0" xmlns="urn:oasis:names:tc:opendocument:xmlns:container">
  <rootfiles>
    <rootfile full-path="OEBPS/content.opf" media-type="application/oebps-package+xml"/>
  </rootfiles>
</container>'''
        epub.writestr('META-INF/container.xml', container_xml, compress_type=zipfile.ZIP_STORED)

        # Package document (content.opf)
        content_opf = '''<?xml version="1.0" encoding="UTF-8"?>
<package xmlns="http://www.idpf.org/2007/opf" version="3.0" unique-identifier="uid">
  <metadata xmlns:dc="http://purl.org/dc/elements/1.1/">
    <dc:identifier id="uid">stored-spike-test-epub</dc:identifier>
    <dc:title>STORED Spike Test</dc:title>
    <dc:language>en</dc:language>
  </metadata>
  <manifest>
    <item id="nav" href="nav.xhtml" media-type="application/xhtml+xml" properties="nav"/>
    <item id="chapter1" href="chapter1.xhtml" media-type="application/xhtml+xml"/>
    <item id="test_image" href="images/test_image.jpg" media-type="image/jpeg"/>
  </manifest>
  <spine>
    <itemref idref="chapter1"/>
  </spine>
</package>'''
        epub.writestr('OEBPS/content.opf', content_opf, compress_type=zipfile.ZIP_STORED)

        # Navigation document (nav.xhtml)
        nav_xhtml = '''<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE html>
<html xmlns="http://www.w3.org/1999/xhtml" xmlns:epub="http://www.idpf.org/2007/ops">
<head><title>Navigation</title></head>
<body>
  <nav epub:type="toc">
    <h1>Contents</h1>
    <ol>
      <li><a href="chapter1.xhtml">Chapter 1</a></li>
    </ol>
  </nav>
</body>
</html>'''
        epub.writestr('OEBPS/nav.xhtml', nav_xhtml, compress_type=zipfile.ZIP_STORED)

        # Chapter content (XHTML)
        chapter1_xhtml = '''<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE html>
<html xmlns="http://www.w3.org/1999/xhtml">
<head><title>Chapter 1</title></head>
<body>
<h1>Chapter 1: A Brief Tale</h1>
<p>This is the first paragraph of our test chapter. It demonstrates that a simple, uncompressed EPUB can contain text content and be read by this project's existing EPUB reader.</p>
<p><img src="images/test_image.jpg" alt="Test image"/></p>
<p>This is the second paragraph, following the embedded image. The EPUB you are reading was created with ZIP_STORED entries only—no DEFLATE compression. This validates that on-device zip writers capable only of emitting uncompressed entries can still produce spec-valid, readable EPUBs.</p>
</body>
</html>'''
        epub.writestr('OEBPS/chapter1.xhtml', chapter1_xhtml, compress_type=zipfile.ZIP_STORED)

        # Generate and embed a minimal JPEG image
        img = create_solid_test_image(200, 200, gray_value=128)
        import io
        img_bytes = io.BytesIO()
        img.save(img_bytes, format='JPEG', quality=85)
        img_data = img_bytes.getvalue()

        epub.writestr('OEBPS/images/test_image.jpg', img_data, compress_type=zipfile.ZIP_STORED)


def main():
    OUTPUT_DIR.mkdir(parents=True, exist_ok=True)

    epub_path = OUTPUT_DIR / 'stored_spike_test.epub'
    print(f"Generating STORED-only EPUB: {epub_path}")

    create_stored_epub(epub_path)

    print(f"EPUB created successfully: {epub_path}")
    print("\nVerifying EPUB integrity...")

    # Verification: check all entries are STORED and mimetype is first
    with zipfile.ZipFile(epub_path, 'r') as epub:
        infolist = epub.infolist()

        print(f"\nTotal entries: {len(infolist)}")
        print("\nEntry details:")
        print(f"{'Filename':<40} {'Compress Type':<15} {'Size':<8}")
        print("-" * 63)

        for i, info in enumerate(infolist):
            compress_type_name = 'STORED' if info.compress_type == zipfile.ZIP_STORED else 'DEFLATED'
            print(f"{info.filename:<40} {compress_type_name:<15} {info.file_size:<8}")

            # Verify all are STORED
            if info.compress_type != zipfile.ZIP_STORED:
                print(f"ERROR: Entry '{info.filename}' is not STORED (compress_type={info.compress_type})")
                sys.exit(1)

        # Verify mimetype is first
        if infolist[0].filename != 'mimetype':
            print(f"\nERROR: First entry is '{infolist[0].filename}', expected 'mimetype'")
            sys.exit(1)

        # Verify mimetype content
        mimetype_content = epub.read('mimetype')
        if mimetype_content != b'application/epub+zip':
            print(f"\nERROR: mimetype content is '{mimetype_content}', expected b'application/epub+zip'")
            sys.exit(1)

        print("\n[OK] All entries use ZIP_STORED")
        print("[OK] 'mimetype' is first entry")
        print("[OK] mimetype content is correct")
        print("\nVerification PASSED")


if __name__ == '__main__':
    main()
