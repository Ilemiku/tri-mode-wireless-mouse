"""生成本机OTA开发证书。私钥仅留在MouseConsole/.local，不加入固件或仓库。

依赖cryptography；首次部署前运行：python Tools/setup_ota_tls.py --host 电脑局域网IP
不修改Windows信任库；ESP32只内置本项目CA，仍核对服务器IP/主机名。
"""
import argparse
import datetime as dt
import ipaddress
from pathlib import Path
from cryptography import x509
from cryptography.hazmat.primitives import hashes, serialization
from cryptography.hazmat.primitives.asymmetric import ec
from cryptography.x509.oid import NameOID, ExtendedKeyUsageOID


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--host", required=True)
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[2]
    local = root / "MouseConsole" / ".local"
    embedded = root / "Esp32Mouse" / ".ota"
    local.mkdir(exist_ok=True)
    embedded.mkdir(exist_ok=True)
    ca_path, key_path = local / "ota-ca.pem", local / "ota-ca-key.pem"
    now = dt.datetime.now(dt.timezone.utc)
    if ca_path.exists() and key_path.exists():
        ca = x509.load_pem_x509_certificate(ca_path.read_bytes())
        ca_key = serialization.load_pem_private_key(key_path.read_bytes(), None)
    elif ca_path.exists() or key_path.exists():
        raise SystemExit("CA文件不完整；不自动覆盖现有证书，请检查.local目录")
    else:
        ca_key = ec.generate_private_key(ec.SECP256R1())
        name = x509.Name([x509.NameAttribute(NameOID.COMMON_NAME, "Mouse Local OTA CA")])
        ca = (x509.CertificateBuilder().subject_name(name).issuer_name(name)
              .public_key(ca_key.public_key()).serial_number(x509.random_serial_number())
              .not_valid_before(now-dt.timedelta(days=1)).not_valid_after(now+dt.timedelta(days=3650))
              .add_extension(x509.BasicConstraints(ca=True, path_length=0), critical=True)
              .add_extension(x509.KeyUsage(False, False, False, False, False, True, True, False, False), critical=True)
              .sign(ca_key, hashes.SHA256()))
        ca_path.write_bytes(ca.public_bytes(serialization.Encoding.PEM))
        key_path.write_bytes(ca_key.private_bytes(serialization.Encoding.PEM,
                            serialization.PrivateFormat.PKCS8, serialization.NoEncryption()))
    leaf_key = ec.generate_private_key(ec.SECP256R1())
    try:
        identity = x509.IPAddress(ipaddress.ip_address(args.host))
    except ValueError:
        identity = x509.DNSName(args.host)
    leaf = (x509.CertificateBuilder()
            .subject_name(x509.Name([x509.NameAttribute(NameOID.COMMON_NAME, args.host)]))
            .issuer_name(ca.subject).public_key(leaf_key.public_key())
            .serial_number(x509.random_serial_number())
            .not_valid_before(now-dt.timedelta(days=1)).not_valid_after(now+dt.timedelta(days=365))
            .add_extension(x509.BasicConstraints(ca=False, path_length=None), critical=True)
            .add_extension(x509.SubjectAlternativeName([identity]), critical=False)
            .add_extension(x509.ExtendedKeyUsage([ExtendedKeyUsageOID.SERVER_AUTH]), critical=False)
            .sign(ca_key, hashes.SHA256()))
    (local / "ota-server.pem").write_bytes(leaf.public_bytes(serialization.Encoding.PEM))
    (local / "ota-server-key.pem").write_bytes(leaf_key.private_bytes(
        serialization.Encoding.PEM, serialization.PrivateFormat.PKCS8, serialization.NoEncryption()))
    (local / "ota-host.txt").write_text(args.host, encoding="utf8")
    (embedded / "ca.pem").write_bytes(ca.public_bytes(serialization.Encoding.PEM))
    print(f"OTA开发证书准备完成，服务器={args.host}:8089；私钥未写入固件")


if __name__ == "__main__":
    main()
