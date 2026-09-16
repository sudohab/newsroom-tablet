// Root-Zertifikat der privaten newsroom21-CA (Caddy).
//
// Kein Geheimnis: Ein Root-Zertifikat ist der oeffentliche Teil und darf in
// Git stehen. Es ist der Anker, gegen den das Tablet die TLS-Verbindung zum
// Pi prueft - ohne dieses Zertifikat koennte sich ein fremder Rechner im
// Heimnetz als newsroom21 ausgeben.
//
// Quelle: newsroom21/newsroom21-root-ca.crt
// subject=CN = Caddy Local Authority - 2026 ECC Root
// notAfter=Jul 23 18:36:02 2036 GMT
//
// Laeuft das Zertifikat ab oder wird die CA neu erzeugt, muss diese Datei
// erneuert und die Firmware neu geflasht werden.
#pragma once

static const char kNewsroomRootCa[] PROGMEM = R"EOF(
-----BEGIN CERTIFICATE-----
MIIBozCCAUmgAwIBAgIQD6XWd4VjjXxepPtFnc4zpDAKBggqhkjOPQQDAjAwMS4w
LAYDVQQDEyVDYWRkeSBMb2NhbCBBdXRob3JpdHkgLSAyMDI2IEVDQyBSb290MB4X
DTI2MDkxNDE4MzYwMloXDTM2MDcyMzE4MzYwMlowMDEuMCwGA1UEAxMlQ2FkZHkg
TG9jYWwgQXV0aG9yaXR5IC0gMjAyNiBFQ0MgUm9vdDBZMBMGByqGSM49AgEGCCqG
SM49AwEHA0IABMV5jLOmwX3O0qJat0GM5MqdanC4xdCyEt1HSfh+KTcU3+KaS95W
Ms61ajMmrfdViTy3RXsasXY+hjSMkLib50+jRTBDMA4GA1UdDwEB/wQEAwIBBjAS
BgNVHRMBAf8ECDAGAQH/AgEBMB0GA1UdDgQWBBTsC/7i72T9ilyjI9RbPva3Gc6Z
6DAKBggqhkjOPQQDAgNIADBFAiEAmqS5VHuRbNeE7W7RzA3rNzQnIf8S1WZ8CRZG
f+gMYyICIFHKk6fSS/tkS7G1uByrEokxxz7R8y/U9Dmmz0f4ozs0
-----END CERTIFICATE-----
)EOF";
