"""
SecureChain Diploma Verifier — GUI (tampilan saja).
Inti keamanan/blockchain = C++ (scdv_verifier.exe). PDF (cap/kunci) = pdf_secure.py.

Jalankan:  python gui/app.py
"""
import os
import json
import threading
import tkinter as tk
from tkinter import ttk, filedialog, messagebox
from datetime import datetime
import shutil

import pdf_secure
import scdv_core

SECURED_FOLDER = os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))), "secured")
MANIFEST_FILE = os.path.join(SECURED_FOLDER, "manifest.json")

BG = "#0f172a"      # slate-900
CARD = "#1e293b"    # slate-800
ACCENT = "#38bdf8"  # sky-400
OK = "#22c55e"
BAD = "#ef4444"
WARN = "#f59e0b"
TXT = "#e2e8f0"
MUTED = "#94a3b8"


def _field(parent, label):
    """Baris label + entry, kembalikan widget entry."""
    row = tk.Frame(parent, bg=CARD)
    row.pack(fill="x", pady=6)
    tk.Label(row, text=label, bg=CARD, fg=MUTED, width=18, anchor="w",
             font=("Segoe UI", 10)).pack(side="left")
    e = tk.Entry(row, font=("Segoe UI", 10), bg="#0b1220", fg=TXT,
                 insertbackground=TXT, relief="flat")
    e.pack(side="left", fill="x", expand=True, ipady=5, padx=(8, 0))
    return e


class App(tk.Tk):
    def __init__(self):
        super().__init__()
        self.title("SecureChain Diploma Verifier (SCDV)")
        self.geometry("760x620")
        self.configure(bg=BG)
        self.minsize(680, 560)

        # Header
        head = tk.Frame(self, bg=BG)
        head.pack(fill="x", padx=24, pady=(20, 8))
        tk.Label(head, text="🔐  SecureChain Diploma Verifier", bg=BG, fg=TXT,
                 font=("Segoe UI Semibold", 18)).pack(anchor="w")
        tk.Label(head, text="Keamanan & blockchain: C++  •  Tampilan: Python",
                 bg=BG, fg=MUTED, font=("Segoe UI", 9)).pack(anchor="w")

        nb = ttk.Notebook(self)
        nb.pack(fill="both", expand=True, padx=24, pady=12)
        style = ttk.Style()
        style.theme_use("default")
        style.configure("TNotebook", background=BG, borderwidth=0)
        style.configure("TNotebook.Tab", background=CARD, foreground=MUTED,
                        padding=(18, 8), font=("Segoe UI", 10))
        style.map("TNotebook.Tab", background=[("selected", ACCENT)],
                  foreground=[("selected", "#06283d")])

        nb.add(self._kampus_tab(nb), text="  🏛  Kampus — Daftarkan  ")
        nb.add(self._verify_tab(nb), text="  🔍  Verifikasi  ")

        # Status bar
        self.status = tk.Label(self, text="Siap.", bg="#0b1220", fg=MUTED,
                               anchor="w", font=("Segoe UI", 9))
        self.status.pack(fill="x", side="bottom", ipady=4)

    # ── Tab Kampus ───────────────────────────────────────────────────────
    def _kampus_tab(self, nb):
        f = tk.Frame(nb, bg=CARD, padx=22, pady=18)

        fl = tk.Frame(f, bg=CARD)
        fl.pack(fill="x", pady=6)
        tk.Label(fl, text="File ijazah (PDF)", bg=CARD, fg=MUTED, width=18,
                 anchor="w", font=("Segoe UI", 10)).pack(side="left")
        self.k_file = tk.Entry(fl, font=("Segoe UI", 10), bg="#0b1220", fg=TXT,
                               insertbackground=TXT, relief="flat")
        self.k_file.pack(side="left", fill="x", expand=True, ipady=5, padx=(8, 6))
        tk.Button(fl, text="Pilih…", command=self._pick_kampus, bg=ACCENT,
                  fg="#06283d", relief="flat", font=("Segoe UI Semibold", 9),
                  padx=12, cursor="hand2").pack(side="left")

        self.k_code = _field(f, "Kode Unik")
        self.k_name = _field(f, "Nama Mahasiswa")
        self.k_id = _field(f, "NIM")
        self.k_code.insert(0, "UGM-010203-HUDZAIFAH")

        btns = tk.Frame(f, bg=CARD)
        btns.pack(fill="x", pady=(14, 6))
        tk.Button(btns, text="Buat PDF Contoh", command=self._make_sample,
                  bg=CARD, fg=ACCENT, relief="solid", bd=1,
                  font=("Segoe UI", 9), padx=12, pady=6, cursor="hand2").pack(side="left")
        tk.Button(btns, text="🔒  Daftarkan & Amankan", command=self._do_register,
                  bg=OK, fg="#06281a", relief="flat",
                  font=("Segoe UI Semibold", 10), padx=18, pady=6,
                  cursor="hand2").pack(side="right")

        self.k_out = tk.Text(f, height=9, bg="#0b1220", fg=TXT, relief="flat",
                             font=("Consolas", 9), wrap="word", padx=10, pady=8)
        self.k_out.pack(fill="both", expand=True, pady=(10, 0))
        self._log(self.k_out, "Isi data lalu klik 'Daftarkan & Amankan'.\n"
                  "Sistem akan: beri cap → tanda tangan metadata → kunci PDF "
                  "dengan kode unik → catat ke blockchain (C++).")
        return f

    # ── Tab Verifikasi ───────────────────────────────────────────────────
    def _verify_tab(self, nb):
        f = tk.Frame(nb, bg=CARD, padx=22, pady=18)

        fl = tk.Frame(f, bg=CARD)
        fl.pack(fill="x", pady=6)
        tk.Label(fl, text="File ijazah (PDF)", bg=CARD, fg=MUTED, width=18,
                 anchor="w", font=("Segoe UI", 10)).pack(side="left")
        self.v_file = tk.Entry(fl, font=("Segoe UI", 10), bg="#0b1220", fg=TXT,
                               insertbackground=TXT, relief="flat")
        self.v_file.pack(side="left", fill="x", expand=True, ipady=5, padx=(8, 6))
        tk.Button(fl, text="Pilih…", command=self._pick_verify, bg=ACCENT,
                  fg="#06283d", relief="flat", font=("Segoe UI Semibold", 9),
                  padx=12, cursor="hand2").pack(side="left")

        self.v_code = _field(f, "Kode Unik")

        tk.Button(f, text="🔍  Verifikasi Sekarang", command=self._do_verify,
                  bg=ACCENT, fg="#06283d", relief="flat",
                  font=("Segoe UI Semibold", 11), pady=8,
                  cursor="hand2").pack(fill="x", pady=(14, 8))

        self.v_panel = tk.Label(f, text="Hasil verifikasi akan tampil di sini.",
                                bg="#0b1220", fg=MUTED, font=("Segoe UI", 11),
                                height=6, wraplength=620, justify="center")
        self.v_panel.pack(fill="both", expand=True, pady=(6, 8))

        tk.Button(f, text="Cek Integritas Blockchain", command=self._do_validate,
                  bg=CARD, fg=MUTED, relief="solid", bd=1, font=("Segoe UI", 9),
                  pady=5, cursor="hand2").pack(fill="x")
        return f

    # ── Helpers ──────────────────────────────────────────────────────────
    def _log(self, widget, text, clear=True):
        if clear:
            widget.delete("1.0", "end")
        widget.insert("end", text)

    def _set_status(self, text):
        self.status.config(text=text)
        self.update_idletasks()

    def _pick_kampus(self):
        p = filedialog.askopenfilename(filetypes=[("PDF", "*.pdf")])
        if p:
            self.k_file.delete(0, "end"); self.k_file.insert(0, p)

    def _pick_verify(self):
        p = filedialog.askopenfilename(filetypes=[("PDF", "*.pdf")])
        if p:
            self.v_file.delete(0, "end"); self.v_file.insert(0, p)

    def _make_sample(self):
        name = self.k_name.get().strip() or "Hudzaifah Rahman"
        sid = self.k_id.get().strip() or "010203"
        p = filedialog.asksaveasfilename(defaultextension=".pdf",
                                         initialfile="ijazah_contoh.pdf",
                                         filetypes=[("PDF", "*.pdf")])
        if not p:
            return
        try:
            pdf_secure.make_sample_pdf(p, name, sid)
            self.k_file.delete(0, "end"); self.k_file.insert(0, p)
            self._set_status(f"PDF contoh dibuat: {p}")
            self._log(self.k_out, f"✅ PDF contoh dibuat:\n{p}\n\n"
                      "Sekarang klik 'Daftarkan & Amankan'.")
        except Exception as e:
            messagebox.showerror("Gagal", str(e))

    # ── Aksi utama ───────────────────────────────────────────────────────
    def _do_register(self):
        src = self.k_file.get().strip()
        code = self.k_code.get().strip()
        name = self.k_name.get().strip()
        sid = self.k_id.get().strip()
        if not (src and code and name and sid):
            messagebox.showwarning("Lengkapi data", "Semua kolom wajib diisi.")
            return
        if not os.path.exists(src):
            messagebox.showerror("File tidak ada", f"Tidak ditemukan:\n{src}")
            return
        self._set_status("Memproses… memberi cap, mengunci, mencatat ke blockchain")
        threading.Thread(target=self._register_worker,
                         args=(src, code, name, sid), daemon=True).start()

    def _register_worker(self, src, code, name, sid):
        try:
            # Buat folder secured jika belum ada
            os.makedirs(SECURED_FOLDER, exist_ok=True)

            # Secure file dengan cap + tanda tangan + kunci
            base, _ = os.path.splitext(src)
            out = base + "_SECURED.pdf"
            sig = pdf_secure.stamp_and_secure(src, out, code, name, sid)
            res = scdv_core.register(out, code, name, sid)

            # Copy file ke folder secured
            if res.get("STATUS") == "OK":
                secured_filename = f"{sid}_{name.replace(' ', '_')}_SECURED.pdf"
                secured_path = os.path.join(SECURED_FOLDER, secured_filename)
                shutil.copy2(out, secured_path)

                # Update manifest.json dengan hash info
                manifest = {}
                if os.path.exists(MANIFEST_FILE):
                    with open(MANIFEST_FILE, 'r') as f:
                        manifest = json.load(f)

                manifest[sid] = {
                    "name": name,
                    "hash": res.get("HASH", ""),
                    "signature": sig,
                    "timestamp": datetime.now().isoformat(),
                    "secured_file": secured_filename
                }

                with open(MANIFEST_FILE, 'w') as f:
                    json.dump(manifest, f, indent=2)

            self.after(0, self._register_done, res, out, sig, code)
        except Exception as e:
            self.after(0, lambda: messagebox.showerror("Gagal", str(e)))
            self.after(0, lambda: self._set_status("Gagal."))

    def _register_done(self, res, out, sig, code):
        if res.get("STATUS") == "OK":
            self._log(self.k_out,
                      "✅ BERHASIL DIDAFTARKAN\n"
                      "──────────────────────────────\n"
                      f"File aman      : {out}\n"
                      f"SHA-256        : {res.get('HASH','')[:48]}…\n"
                      f"Tanda tangan   : {sig[:48]}…\n"
                      f"Kode pembuka   : {code}\n\n"
                      "📁 Disimpan ke  : secured/ folder\n"
                      "📋 Manifest     : secured/manifest.json\n\n"
                      "→ Berikan FILE *_SECURED.pdf ini ke mahasiswa.\n"
                      "→ File hanya bisa DIBUKA dengan kode unik di atas.\n"
                      "→ Hash sudah tercatat permanen di blockchain (C++).")
            self._set_status("Selesai — ijazah aman terdaftar di folder secured/.")
            if messagebox.askyesno("Berhasil", "Ijazah aman & terdaftar.\nBuka folder 'secured'?"):
                os.startfile(SECURED_FOLDER)
        else:
            msg = res.get("MESSAGE", "Tidak diketahui")
            self._log(self.k_out, f"❌ GAGAL: {msg}")
            self._set_status("Gagal mencatat ke blockchain.")

    def _do_verify(self):
        path = self.v_file.get().strip()
        code = self.v_code.get().strip()
        if not (path and code):
            messagebox.showwarning("Lengkapi data", "Isi file dan kode unik.")
            return
        if not os.path.exists(path):
            messagebox.showerror("File tidak ada", f"Tidak ditemukan:\n{path}")
            return
        self._set_status("Memverifikasi…")
        threading.Thread(target=self._verify_worker, args=(path, code),
                         daemon=True).start()

    def _verify_worker(self, path, code):
        try:
            res = scdv_core.verify(path, code)
            opens = pdf_secure.can_open(path, code)
            self.after(0, self._verify_done, res, opens)
        except Exception as e:
            self.after(0, lambda: messagebox.showerror("Gagal", str(e)))
            self.after(0, lambda: self._set_status("Gagal."))

    def _verify_done(self, res, opens):
        st = res.get("STATUS")
        if st == "VERIFIED":
            self.v_panel.config(
                bg="#052e1a", fg=OK,
                text=("✅  IJAZAH ASLI — TERVERIFIKASI\n\n"
                      f"Nama : {res.get('NAME','')}\n"
                      f"NIM  : {res.get('ID','')}\n"
                      f"Terdaftar : {res.get('TIME','')}\n\n"
                      f"Double-Lock: Integritas SHA-256 ✓   Kode Unik ✓\n"
                      f"File bisa dibuka dengan kode: {'✓' if opens else '✗'}"))
            self._set_status("VERIFIED — dokumen asli.")
        elif st == "FAILED":
            self.v_panel.config(
                bg="#2e0505", fg=BAD,
                text=("❌  TIDAK VALID / PALSU\n\n"
                      "Penyebab salah satu dari:\n"
                      "• File sudah diubah (hash tidak cocok)\n"
                      "• Kode unik salah\n"
                      "• Dokumen tidak terdaftar di blockchain"))
            self._set_status("FAILED — dokumen tidak terverifikasi.")
        else:
            self.v_panel.config(bg="#2e1f05", fg=WARN,
                                text=f"⚠  ERROR: {res.get('MESSAGE','tidak diketahui')}")
            self._set_status("Error.")

    def _do_validate(self):
        try:
            res = scdv_core.validate()
            if res.get("STATUS") == "VALID":
                messagebox.showinfo("Integritas", "✅ Blockchain VALID & bebas manipulasi.")
            else:
                messagebox.showwarning("Integritas", "❌ Blockchain TIDAK valid — terdeteksi manipulasi!")
        except Exception as e:
            messagebox.showerror("Gagal", str(e))


if __name__ == "__main__":
    App().mainloop()
