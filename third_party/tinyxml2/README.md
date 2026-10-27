# TinyXML2 11.0.0

Based on upstream files from https://github.com/leethomason/tinyxml2/tree/9148bdf719e997d1f474be6bcc7943881046dba1.
License: zlib (LICENSE.txt). Used privately by the model metadata reader.
The including translation units rename the namespace using a local macro, avoiding
symbol collisions with applications linking their own TinyXML2.
No public headers or additional consumer dependencies are introduced.

SHA-256 of original upstream bytes:
- `tinyxml2.cpp`: `95f64e4cb06c9e147d5b12e8860c88898944858dfbc38fed92ebf918f1436554`
- `tinyxml2.h`: `0f3c6e5a91b6d65caca3cc7239926455f21e488cff80ac3d27c74f5262fd2602`
- `LICENSE.txt`: `9332252e9b9e46db8285d4a3f0bf25f139bf1dca6781b956d57f2302efca6432`

Local modification: `XMLDocument::Identify` retains all whitespace-only text nodes
in PEDANTIC_WHITESPACE mode, including next to comments and CDATA. Upstream only
retains such text immediately before the first closing tag. Other modes are unchanged.
A second local change rejects orphan document-level closing tags, which otherwise
terminate parsing successfully and hide trailing content. Both changes are marked
in the source and covered by metadata regression tests.

Vendored source line endings are normalized to LF. SHA-256 of repository files:
- `tinyxml2.cpp`: `8f6054c70fbfd660991b6cf736dc87e55950d34cd44dea0e227fd89ac91017d6`
- `tinyxml2.h`: `79923d1f8e56cc454c4edda228943d682c1581d2532219687c459ca2c8463070`
- `LICENSE.txt`: `9332252e9b9e46db8285d4a3f0bf25f139bf1dca6781b956d57f2302efca6432`
