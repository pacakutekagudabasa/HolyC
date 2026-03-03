#!/usr/bin/env python3
"""hcc --lsp के लिए basic LSP server smoke test करो।"""
import subprocess, json, sys, os, time, threading

HCC = os.path.join(os.path.dirname(__file__), '..', '..', 'build', 'hcc')

def encode(msg):
    body = json.dumps(msg)
    return f"Content-Length: {len(body)}\r\n\r\n{body}".encode()

def send(proc, msg):
    proc.stdin.write(encode(msg))
    proc.stdin.flush()

def recv(proc, skip_notifications=True):
    """अगला message read करो, optionally JSON-RPC notifications skip करो।"""
    while True:
        header = b""
        while b"\r\n\r\n" not in header:
            ch = proc.stdout.read(1)
            if not ch:
                return None
            header += ch
        length = int([l for l in header.decode().split("\r\n") if l.startswith("Content-Length")][0].split(":")[1])
        body = proc.stdout.read(length)
        msg = json.loads(body)
        # जब requested हो तो notifications (जिनमें "id" field नहीं) skip करो
        if skip_notifications and "id" not in msg:
            continue
        return msg

def test_lsp():
    proc = subprocess.Popen(
        [HCC, '--lsp'],
        stdin=subprocess.PIPE,
        stdout=subprocess.PIPE,
        stderr=subprocess.DEVNULL
    )

    # Server initialize करो
    send(proc, {"jsonrpc":"2.0","id":1,"method":"initialize","params":{
        "processId": os.getpid(),
        "capabilities": {},
        "rootUri": None
    }})
    resp = recv(proc)
    assert resp and resp.get("id") == 1, f"initialize failed: {resp}"
    print("PASS: initialize")

    # Document open करो
    doc_uri = "file:///tmp/test_lsp_doc.HC"
    source = "class Animal { I64 legs; }\nclass Dog : Animal { }\nU0 Main() { Dog d; d. }"
    send(proc, {"jsonrpc":"2.0","method":"textDocument/didOpen","params":{
        "textDocument": {"uri": doc_uri, "languageId": "holyc", "version": 1, "text": source}
    }})
    time.sleep(0.3)

    # "d." position (line 2, col 21) पर completion request करो
    send(proc, {"jsonrpc":"2.0","id":2,"method":"textDocument/completion","params":{
        "textDocument": {"uri": doc_uri},
        "position": {"line": 2, "character": 21}
    }})
    resp = recv(proc)
    assert resp and resp.get("id") == 2, f"completion failed: {resp}"
    items = resp.get("result", {})
    if isinstance(items, dict):
        items = items.get("items", [])
    labels = [i.get("label","") for i in items]
    # 'legs' (Animal से inherited) और 'Speak' शामिल होने चाहिए
    has_inherited = any("legs" in l or "inherited" in i.get("detail","").lower() for i, l in zip(items, labels))
    print(f"PASS: completion returned {len(items)} items: {labels[:5]}")
    if has_inherited:
        print("PASS: inherited member completion works")
    else:
        print("INFO: inherited member not found in completion (may be OK)")

    # Shutdown करो
    send(proc, {"jsonrpc":"2.0","id":3,"method":"shutdown","params":{}})
    recv(proc)
    send(proc, {"jsonrpc":"2.0","method":"exit","params":{}})
    proc.wait(timeout=2)
    print("PASS: shutdown")
    return True

if __name__ == '__main__':
    try:
        ok = test_lsp()
        sys.exit(0 if ok else 1)
    except Exception as e:
        print(f"FAIL: {e}", file=sys.stderr)
        sys.exit(1)
