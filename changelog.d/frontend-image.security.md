The frontend image takes the Alpine package fixes at build time, which brings
OpenSSL to 3.3.7 (CVE-2026-31789); 1.23.0 was not deployed because its image
scan failed on that CVE.
