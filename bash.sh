#!/usr/bin/env python3
"""
AF_ALG AEAD Scatterwalk Fuzzer
针对 gcm, ccm, xts, authencesn 等模板，测试碎片化 splice 与 recvmsg 的交互。
"""
import os, socket, struct, sys

AF_ALG = 38
SOL_ALG = 279

# 可替换为目标模板，如 "ccm(aes)", "gcm(aes)", "rfc4106(gcm(aes))"
TEMPLATES = [
    "authencesn(hmac(sha256),cbc(aes))",
    "gcm(aes)",
    "ccm(aes)",
]

def try_template(name):
    try:
        tfm = socket.socket(AF_ALG, socket.SOCK_SEQPACKET, 0)
        tfm.bind(("aead", name))
        # 16 字节零密钥
        tfm.setsockopt(SOL_ALG, 1, b'\x00' * 16)
        op, _ = tfm.accept()
        print(f"[+] {name}: socket bound OK")
        
        # 创建 pipe 并写入碎片化数据（模拟 Copy Fail 的 SPLICE_F_MORE 模式）
        r, w = os.pipe()
        os.write(w, b'\x00' * 200)
        
        # 逐字节 splice，最后一个字节不带 SPLICE_F_MORE
        for i in range(200):
            flags = os.SPLICE_F_MORE if i < 199 else 0
            os.splice(r, op.fileno(), 1, flags)
        
        # 触发 scatterwalk：recvmsg 期望 authsize + plaintext
        out = bytearray(200 + 16)
        try:
            op.recv_into(out)
            print(f"[-] {name}: recv completed without panic")
        except OSError as e:
            print(f"[!] {name}: recv failed: {e}")
        op.close()
        tfm.close()
        return True
    except OSError as e:
        print(f"[!] {name}: setup failed: {e}")
        return False

if __name__ == "__main__":
    for t in TEMPLATES:
        try_template(t)
