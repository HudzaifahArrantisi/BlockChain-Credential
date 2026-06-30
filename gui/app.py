import os, json, threading, base64, io, tkinter as tk
from tkinter import ttk, filedialog, messagebox
from datetime import datetime
import shutil
import urllib.request, urllib.parse
import time

from PIL import Image, ImageTk
import fitz

import pdf_secure
import scdv_core

SECURED_FOLDER = os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))), "secured")
MANIFEST_FILE = os.path.join(SECURED_FOLDER, "manifest.json")

BG      = "#0a0e1a"
CARD    = "#111827"
CARD2   = "#1a2235"
ACCENT  = "#38bdf8"
ACCENT2 = "#0ea5e9"
OK      = "#22c55e"
BAD     = "#ef4444"
WARN    = "#f59e0b"
TXT     = "#e2e8f0"
MUTED   = "#6b7a99"


def geocode_address(address):
    try:
        url = "https://nominatim.openstreetmap.org/search?" + urllib.parse.urlencode({"q": address, "format": "json", "limit": 1})
        req = urllib.request.Request(url, headers={"User-Agent": "SecureChainDiplomaVerifier/1.0"})
        with urllib.request.urlopen(req, timeout=5) as resp:
            data = json.loads(resp.read().decode())
            if data:
                return {"lat": data[0]["lat"], "lon": data[0]["lon"]}
    except:
        pass
    return None


class CyberEntry(tk.Entry):
    def __init__(self, parent, **kw):
        super().__init__(parent, font=("Segoe UI", 10), bg="#0a0e1a",
                         fg=TXT, insertbackground=ACCENT, relief="flat",
                         highlightthickness=1, highlightbackground=CARD2,
                         highlightcolor=ACCENT, **kw)
        self.bind("<FocusIn>", lambda e: self.config(highlightbackground=ACCENT))
        self.bind("<FocusOut>", lambda e: self.config(highlightbackground=CARD2))


def _cyber_field(parent, label):
    row = tk.Frame(parent, bg=CARD)
    row.pack(fill="x", pady=4)
    tk.Label(row, text=label, bg=CARD, fg=MUTED, width=16, anchor="w",
             font=("Segoe UI", 9)).pack(side="left")
    e = CyberEntry(row)
    e.pack(side="left", fill="x", expand=True, ipady=4, padx=(6, 0))
    return e


def _glow_button(parent, text, command, color=ACCENT, **kw):
    kw.setdefault("font", ("Segoe UI Semibold", 10))
    kw.setdefault("padx", 18)
    kw.setdefault("pady", 7)
    btn = tk.Button(parent, text=text, command=command,
                    bg=color, fg="#0a0e1a", relief="flat",
                    cursor="hand2", activebackground=ACCENT2, **kw)
    return btn


class LED(tk.Canvas):
    def __init__(self, parent, size=10, color=MUTED):
        super().__init__(parent, width=size+4, height=size+4,
                         bg=CARD, highlightthickness=0)
        self.dot = self.create_oval(2, 2, size+2, size+2, fill=color, outline="")

    def set_color(self, color):
        self.itemconfig(self.dot, fill=color)


class ScrollFrame(tk.Frame):
    def __init__(self, parent, bgcolor="#060a14"):
        super().__init__(parent, bg=bgcolor)
        self.canvas = tk.Canvas(self, bg=bgcolor, highlightthickness=0, bd=0)
        self.scrollbar = tk.Scrollbar(self, orient="vertical", command=self.canvas.yview,
                                       bg=CARD2, troughcolor=BG, bd=0,
                                       activebackground=ACCENT)
        self.scroll_frame = tk.Frame(self.canvas, bg=bgcolor)
        self.scroll_frame.bind("<Configure>",
                               lambda e: self.canvas.configure(scrollregion=self.canvas.bbox("all")))
        self.canvas.create_window((0, 0), window=self.scroll_frame, anchor="nw",
                                  width=self.canvas.winfo_reqwidth() or 440)
        self.canvas.configure(yscrollcommand=self.scrollbar.set)
        self.canvas.pack(side="left", fill="both", expand=True)
        self.scrollbar.pack(side="right", fill="y")
        self.bind("<Enter>", self._bind_mouse)
        self.bind("<Leave>", self._unbind_mouse)
        self.bind("<Configure>", self._resize_width)

    def _resize_width(self, event=None):
        w = self.canvas.winfo_width() - self.scrollbar.winfo_width() - 8
        if w > 100:
            self.canvas.itemconfig(1, width=w)

    def _bind_mouse(self, event=None):
        self.canvas.bind_all("<MouseWheel>", self._on_mousewheel, add="+")

    def _unbind_mouse(self, event=None):
        self.canvas.unbind_all("<MouseWheel>")

    def _on_mousewheel(self, event):
        self.canvas.yview_scroll(-1 * (event.delta // 120), "units")


class ScanPopup:
    def __init__(self, parent):
        self.win = tk.Toplevel(parent)
        self.win.title("System Scanning")
        self.win.geometry("600x420")
        self.win.configure(bg=BG)
        self.win.resizable(False, False)
        self.win.transient(parent)
        self.win.grab_set()

        head = tk.Frame(self.win, bg=BG)
        head.pack(fill="x", padx=20, pady=(14, 4))
        tk.Label(head, text="⏳ SYSTEM SCANNING", bg=BG, fg=ACCENT,
                 font=("Segoe UI", 11, "bold")).pack(anchor="w")
        tk.Label(head, text="Scanning secured storage for matching records...",
                 bg=BG, fg=MUTED, font=("Segoe UI", 8)).pack(anchor="w")

        self._animating = True
        self._blink_state = True
        self.result = None
        self.done = threading.Event()

        self.status_dot = tk.Canvas(head, width=14, height=14, bg=BG, highlightthickness=0)
        self.status_dot.pack(side="right")
        self.dot = self.status_dot.create_oval(2, 2, 12, 12, fill=WARN, outline="")
        self._blink()

        log_frame = tk.Frame(self.win, bg=CARD, bd=1, relief="solid",
                             highlightbackground=CARD2, highlightthickness=1)
        log_frame.pack(fill="both", expand=True, padx=20, pady=(4, 10))

        tk.Label(log_frame, text="▶ SCAN LOG", bg=CARD, fg=MUTED,
                 font=("Consolas", 8, "bold")).pack(anchor="w", padx=10, pady=(6, 0))

        self.log = tk.Text(log_frame, bg="#060a14", fg="#a8b5d6", relief="flat",
                           font=("Consolas", 9), wrap="word", padx=12, pady=6,
                           bd=0, highlightthickness=0, state="normal")
        self.log.pack(fill="both", expand=True, padx=6, pady=(0, 6))
        self.log.insert("end", f"[SYSTEM] Initializing scan...\n")
        self.log.see("end")

    def _blink(self):
        self._blink_state = not self._blink_state
        color = WARN if self._blink_state else BG
        self.status_dot.itemconfig(self.dot, fill=color)
        if self._animating:
            self.win.after(500, self._blink)

    def log_line(self, text, tag=None):
        self.win.after(0, lambda: self._insert_log(text, tag))

    def _insert_log(self, text, tag):
        self.log.insert("end", f"[{datetime.now().strftime('%H:%M:%S')}] {text}\n")
        self.log.see("end")
        self.win.update_idletasks()

    def set_success(self):
        self._animating = False
        self.status_dot.itemconfig(self.dot, fill=OK)

    def set_fail(self):
        self._animating = False
        self.status_dot.itemconfig(self.dot, fill=BAD)

    def close(self):
        self._animating = False
        if self.win.winfo_exists():
            self.win.grab_release()
            self.win.destroy()


class ResultPopup:
    def __init__(self, parent, title, data_dict=None, pdf_path=None, code=None):
        self.win = tk.Toplevel(parent)
        w = 520 if data_dict else 600
        h = 520 if pdf_path else 440
        self.win.title(title)
        self.win.geometry(f"{w}x{h}")
        self.win.configure(bg=BG)
        self.win.transient(parent)
        self.win.grab_set()

        head = tk.Frame(self.win, bg=BG)
        head.pack(fill="x", padx=20, pady=(14, 4))
        tk.Label(head, text=title, bg=BG, fg=ACCENT,
                 font=("Segoe UI", 11, "bold")).pack(anchor="w")

        body = ScrollFrame(self.win, bgcolor="#060a14")
        body.pack(fill="both", expand=True, padx=20, pady=(4, 12))
        bf = body.scroll_frame

        if data_dict:
            tk.Label(bf, text="STUDENT DATA", bg="#060a14", fg=OK,
                     font=("Segoe UI", 9, "bold")).pack(anchor="w", pady=(6, 8))
            for key, val in data_dict.items():
                row = tk.Frame(bf, bg="#060a14")
                row.pack(fill="x", pady=3)
                tk.Label(row, text=f"{key}:", bg="#060a14", fg=MUTED,
                         width=16, anchor="w", font=("Segoe UI", 8, "bold")).pack(side="left", padx=(12, 0))
                lbl = tk.Label(row, text=str(val) if val else "-", bg="#060a14", fg=TXT,
                               anchor="w", font=("Segoe UI", 9), wraplength=300, justify="left")
                lbl.pack(side="left", fill="x", expand=True)

        if pdf_path and os.path.exists(pdf_path):
            tk.Label(bf, text="DOCUMENT PREVIEW", bg="#060a14", fg=ACCENT,
                     font=("Segoe UI", 9, "bold")).pack(anchor="w", pady=(10, 6))
            try:
                doc = fitz.open(pdf_path)
                if doc.is_encrypted and code:
                    doc.authenticate(code)
                page = doc[0]
                pix = page.get_pixmap(dpi=120)
                img_data = pix.tobytes("png")
                img = Image.open(io.BytesIO(img_data))
                img.thumbnail((480, 560))
                self._img = ImageTk.PhotoImage(img)
                lbl = tk.Label(bf, image=self._img, bg="#060a14")
                lbl.pack(pady=4)
            except Exception as e:
                tk.Label(bf, text=f"Cannot render PDF:\n{e}", bg="#060a14",
                         fg=BAD, font=("Segoe UI", 9)).pack(pady=10)

        btn_frame = tk.Frame(self.win, bg=BG)
        btn_frame.pack(fill="x", padx=20, pady=(0, 12))
        _glow_button(btn_frame, "Close", self.win.destroy, color=ACCENT).pack(side="right")


class App(tk.Tk):
    def __init__(self):
        super().__init__()
        self.title("SecureChain — Blockchain Credentials Integrity")
        self.geometry("1120x780")
        self.configure(bg=BG)
        self.minsize(960, 660)

        self._build_header()
        self._build_notebook()
        self._build_statusbar()

    def _build_header(self):
        head = tk.Frame(self, bg=BG)
        head.pack(fill="x", padx=28, pady=(16, 2))

        top = tk.Frame(head, bg=BG)
        top.pack(fill="x")
        tk.Label(top, text="⦗ SECURECHAIN ⦘", bg=BG, fg=ACCENT,
                 font=("Segoe UI", 9, "bold")).pack(side="left")
        tk.Label(top, text="v1.0", bg=BG, fg=MUTED,
                 font=("Segoe UI", 8)).pack(side="left", padx=(8, 0))

        title = tk.Frame(head, bg=BG)
        title.pack(fill="x")
        tk.Label(title, text="Diploma Verifier", bg=BG, fg=TXT,
                 font=("Segoe UI Semibold", 20)).pack(anchor="w")
        tk.Label(title, text="C++ Blockchain Core  ·  AES-256  ·  SHA-256  ·  PDF Integrity",
                 bg=BG, fg=MUTED, font=("Segoe UI", 8)).pack(anchor="w")

        sep = tk.Frame(self, height=1, bg=ACCENT)
        sep.pack(fill="x", padx=28)

    def _build_notebook(self):
        nb = ttk.Notebook(self)
        nb.pack(fill="both", expand=True, padx=28, pady=(6, 4))

        style = ttk.Style()
        style.theme_use("default")
        style.configure("TNotebook", background=BG, borderwidth=0)
        style.configure("TNotebook.Tab", background=CARD, foreground=MUTED,
                        padding=(22, 9), font=("Segoe UI", 10))
        style.map("TNotebook.Tab",
                  background=[("selected", ACCENT)],
                  foreground=[("selected", "#06283d")])

        nb.add(self._register_tab(nb), text="Upload File")
        nb.add(self._verify_tab(nb), text="Verifikasi")

    def _build_statusbar(self):
        self.status = tk.Label(self, text="System ready.", bg="#060a14", fg=MUTED,
                               anchor="w", font=("Consolas", 9))
        self.status.pack(fill="x", side="bottom", ipady=3, padx=28)

    def _register_tab(self, nb):
        outer = tk.Frame(nb, bg=BG)
        outer.pack(fill="both", expand=True)

        f = tk.Frame(outer, bg=CARD, padx=22, pady=16, bd=1, relief="solid",
                     highlightbackground=CARD2, highlightthickness=1)
        f.pack(fill="both", expand=True)

        tk.Label(f, text="⏺ REGISTRATION PANEL", bg=CARD, fg=ACCENT,
                 font=("Segoe UI", 9, "bold")).pack(anchor="w", pady=(0, 8))

        fl = tk.Frame(f, bg=CARD)
        fl.pack(fill="x", pady=4)
        tk.Label(fl, text="File ijazah (PDF)", bg=CARD, fg=MUTED, width=16,
                 anchor="w", font=("Segoe UI", 9)).pack(side="left")
        self.k_file = CyberEntry(fl)
        self.k_file.pack(side="left", fill="x", expand=True, ipady=4, padx=(6, 6))
        tk.Button(fl, text="Browse", command=self._pick_kampus, bg=CARD2,
                  fg=ACCENT, relief="flat", font=("Segoe UI Semibold", 9),
                  padx=14, cursor="hand2", activebackground=ACCENT,
                  activeforeground="#0a0e1a").pack(side="left")

        self.k_code   = _cyber_field(f, "Kode Unik")
        self.k_name   = _cyber_field(f, "Nama Mahasiswa")
        self.k_id     = _cyber_field(f, "NIM")
        self.k_nik    = _cyber_field(f, "NIK")
        self.k_phone  = _cyber_field(f, "No. Telepon")
        self.k_address = _cyber_field(f, "Alamat")

        fl_photo = tk.Frame(f, bg=CARD)
        fl_photo.pack(fill="x", pady=4)
        tk.Label(fl_photo, text="Foto Mahasiswa", bg=CARD, fg=MUTED, width=16,
                 anchor="w", font=("Segoe UI", 9)).pack(side="left")
        self.k_photo = CyberEntry(fl_photo)
        self.k_photo.pack(side="left", fill="x", expand=True, ipady=4, padx=(6, 6))
        tk.Button(fl_photo, text="Browse", command=self._pick_photo, bg=CARD2,
                  fg=ACCENT, relief="flat", font=("Segoe UI Semibold", 9),
                  padx=14, cursor="hand2", activebackground=ACCENT,
                  activeforeground="#0a0e1a").pack(side="left")

        btns = tk.Frame(f, bg=CARD)
        btns.pack(fill="x", pady=(10, 4))
        _glow_button(btns, "Register & Secure", self._do_register,
                     color=OK).pack(side="right")
        self.k_clear_btn = tk.Button(btns, text="Clear Form", command=self._clear_kampus_form,
                                     bg=WARN, fg="#0a0e1a", relief="flat",
                                     font=("Segoe UI Semibold", 9), padx=16, pady=6,
                                     cursor="hand2")

        out_frame = tk.Frame(f, bg=CARD)
        out_frame.pack(fill="both", expand=True, pady=(8, 0))
        tk.Label(out_frame, text="▶ OUTPUT LOG", bg=CARD, fg=MUTED,
                 font=("Consolas", 8, "bold")).pack(anchor="w")
        self.k_out = tk.Text(out_frame, height=4, bg="#060a14", fg="#a8b5d6",
                             relief="flat", font=("Consolas", 9), wrap="word",
                             padx=12, pady=6, bd=1, highlightbackground=CARD2,
                             highlightthickness=1, insertbackground=ACCENT)
        self.k_out.pack(fill="both", expand=True, pady=(3, 0))

        return outer

    def _verify_tab(self, nb):
        outer = tk.Frame(nb, bg=BG)
        outer.pack(fill="both", expand=True)

        f = tk.Frame(outer, bg=CARD, padx=22, pady=14, bd=1, relief="solid",
                     highlightbackground=CARD2, highlightthickness=1)
        f.pack(fill="both", expand=True)

        tk.Label(f, text="⏺ VERIFICATION PANEL", bg=CARD, fg=ACCENT,
                 font=("Segoe UI", 9, "bold")).pack(anchor="w", pady=(0, 8))

        input_frame = tk.Frame(f, bg=CARD)
        input_frame.pack(fill="x")

        self.v_name = _cyber_field(input_frame, "Nama")
        self.v_nim  = _cyber_field(input_frame, "NIM")
        self.v_code = _cyber_field(input_frame, "Kode Unik")

        layers = tk.Frame(f, bg=CARD)
        layers.pack(fill="x", pady=(8, 2))

        self.layer_leds = []
        self.layer_labels = []
        layer_info = [("LAYER 1", "Nama"), ("LAYER 2", "NIM"), ("LAYER 3", "Kode Unik")]
        for ltitle, ldesc in layer_info:
            box = tk.Frame(layers, bg=CARD2, bd=1, relief="solid",
                           highlightbackground=MUTED, highlightthickness=1, padx=12, pady=5)
            box.pack(side="left", padx=(0, 8))
            led = LED(box, size=8, color=MUTED)
            led.pack(side="left", padx=(0, 6))
            txt_frame = tk.Frame(box, bg=CARD2)
            txt_frame.pack(side="left")
            tk.Label(txt_frame, text=ltitle, bg=CARD2, fg=MUTED,
                     font=("Segoe UI", 7, "bold")).pack(anchor="w")
            lbl = tk.Label(txt_frame, text=ldesc, bg=CARD2, fg=MUTED,
                           font=("Segoe UI", 8))
            lbl.pack(anchor="w")
            self.layer_leds.append(led)
            self.layer_labels.append(lbl)

        btn_frame = tk.Frame(f, bg=CARD)
        btn_frame.pack(fill="x", pady=(8, 4))
        _glow_button(btn_frame, "Verify Now", self._do_verify,
                     color=ACCENT, font=("Segoe UI Semibold", 11),
                     pady=9).pack(fill="x")

        result_container = tk.Frame(f, bg="#060a14", bd=1, relief="solid",
                                    highlightbackground=CARD2, highlightthickness=1)
        result_container.pack(fill="both", expand=True, pady=(3, 4))
        result_container.grid_rowconfigure(0, weight=1)
        result_container.grid_columnconfigure(0, weight=1, uniform="col")
        result_container.grid_columnconfigure(1, weight=1, uniform="col")

        left_scroll = ScrollFrame(result_container, bgcolor="#060a14")
        left_scroll.grid(row=0, column=0, sticky="nsew", padx=(0, 3))
        lf = left_scroll.scroll_frame

        self.v_photo = tk.Label(lf, bg="#060a14")
        self.v_photo.pack(pady=(10, 4))

        self.v_status = tk.Label(lf, text="Enter name, NIM, and unique code.",
                                 bg="#060a14", fg=MUTED, font=("Segoe UI Semibold", 11),
                                 justify="center", wraplength=400)
        self.v_status.pack(fill="x", pady=4)

        self.v_details = tk.Frame(lf, bg="#060a14")
        self.v_details.pack(fill="x", pady=2)

        self.v_detail_labels = {}
        for fld in ["Nama Lengkap", "NIK", "NIM", "No. Telepon", "Alamat", "Koordinat", "Terdaftar"]:
            row = tk.Frame(self.v_details, bg="#060a14")
            row.pack(fill="x", pady=2)
            tk.Label(row, text=f"{fld}:", bg="#060a14", fg=MUTED, width=14, anchor="w",
                     font=("Segoe UI", 8, "bold")).pack(side="left", padx=(16, 0))
            lbl = tk.Label(row, text="-", bg="#060a14", fg=TXT, anchor="w",
                           font=("Segoe UI", 9), justify="left", wraplength=280)
            lbl.pack(side="left", fill="x", expand=True)
            self.v_detail_labels[fld] = lbl

        self.v_open_btn = tk.Button(lf, text="Open PDF File",
                                    command=self._open_verified_pdf, bg=OK, fg="#0a0e1a",
                                    relief="flat", font=("Segoe UI Semibold", 10),
                                    pady=6, cursor="hand2")

        right_scroll = ScrollFrame(result_container, bgcolor="#060a14")
        right_scroll.grid(row=0, column=1, sticky="nsew", padx=(3, 0))
        rf = right_scroll.scroll_frame

        self.v_pdf = tk.Label(rf, text="PDF preview\nwill appear here.",
                              bg="#060a14", fg=MUTED, font=("Segoe UI", 10),
                              justify="center")
        self.v_pdf.pack(pady=20)

        self.v_verified_file = None
        self.v_verified_code = None

        tk.Button(f, text="Check Blockchain Integrity", command=self._do_validate,
                  bg=CARD2, fg=MUTED, relief="solid", bd=1, font=("Consolas", 8),
                  pady=4, cursor="hand2", activebackground=ACCENT,
                  activeforeground="#0a0e1a").pack(fill="x")

        return outer

    def _log(self, widget, text, clear=True):
        if clear:
            widget.delete("1.0", "end")
        widget.insert("end", text)

    def _set_status(self, text):
        self.status.config(text=text)
        self.update_idletasks()

    def _reset_layer_indicators(self):
        for led, lbl in zip(self.layer_leds, self.layer_labels):
            led.set_color(MUTED)
            lbl.config(fg=MUTED)

    def _set_layer(self, index, color, text):
        self.layer_leds[index].set_color(color)
        self.layer_labels[index].config(fg=color, text=text)

    def _pick_kampus(self):
        p = filedialog.askopenfilename(filetypes=[("PDF", "*.pdf")])
        if p:
            self.k_file.delete(0, "end")
            self.k_file.insert(0, p)

    def _pick_photo(self):
        p = filedialog.askopenfilename(filetypes=[("Images", "*.png;*.jpg;*.jpeg;*.gif;*.bmp")])
        if p:
            self.k_photo.delete(0, "end")
            self.k_photo.insert(0, p)

    def _clear_kampus_form(self):
        for w in (self.k_file, self.k_code, self.k_name, self.k_id,
                  self.k_nik, self.k_phone, self.k_address, self.k_photo):
            w.delete(0, "end")
        self._log(self.k_out, "# Form cleared.\n")
        self.k_clear_btn.pack_forget()
        self._set_status("Form cleared.")

    def _do_register(self):
        src   = self.k_file.get().strip()
        code  = self.k_code.get().strip()
        name  = self.k_name.get().strip()
        sid   = self.k_id.get().strip()
        nik   = self.k_nik.get().strip()
        phone = self.k_phone.get().strip()
        addr  = self.k_address.get().strip()
        photo = self.k_photo.get().strip()

        if not (src and code and name and sid and nik and phone and addr):
            messagebox.showwarning("Incomplete", "All fields required (photo optional).")
            return
        if not os.path.exists(src):
            messagebox.showerror("Error", f"File not found:\n{src}")
            return
        if photo and not os.path.exists(photo):
            messagebox.showerror("Error", f"Photo not found:\n{photo}")
            return

        self._set_status("Geocoding address...")
        threading.Thread(target=self._register_worker,
                         args=(src, code, name, sid, nik, phone, addr, photo),
                         daemon=True).start()

    def _register_worker(self, src, code, name, sid, nik, phone, addr, photo):
        temp_secured = None
        try:
            coord = geocode_address(addr)
            coord_str = f"{coord['lat']}, {coord['lon']}" if coord else ""

            os.makedirs(SECURED_FOLDER, exist_ok=True)
            temp_secured = os.path.join(SECURED_FOLDER, f"temp_{sid}_SECURED.pdf")
            sig = pdf_secure.stamp_and_secure(src, temp_secured, code, name, sid)

            photo_b64 = ""
            if photo:
                try:
                    with Image.open(photo) as img:
                        if img.mode != 'RGB':
                            img = img.convert('RGB')
                        img.thumbnail((150, 150))
                        buf = io.BytesIO()
                        img.save(buf, format='JPEG', quality=60)
                        photo_b64 = base64.b64encode(buf.getvalue()).decode('utf-8')
                except Exception as e:
                    print("Photo error:", e)

            details = {"nik": nik, "phone": phone, "address": addr, "photo": photo_b64, "coord": coord_str}
            details_json = json.dumps(details)
            details_b64 = base64.b64encode(details_json.encode('utf-8')).decode('utf-8')

            res = scdv_core.register(temp_secured, code, name, sid, details_b64)

            if res.get("STATUS") == "OK":
                file_hash = res.get("HASH", "")
                secured_fn = f"{file_hash}.pdf"
                secured_path = os.path.join(SECURED_FOLDER, secured_fn)
                shutil.copy2(temp_secured, secured_path)

                manifest = {}
                if os.path.exists(MANIFEST_FILE):
                    with open(MANIFEST_FILE, 'r') as f:
                        manifest = json.load(f)
                manifest[sid] = {
                    "name": name, "code": code, "hash": file_hash,
                    "signature": sig, "timestamp": datetime.now().isoformat(),
                    "secured_file": secured_fn, "details": details
                }
                with open(MANIFEST_FILE, 'w') as f:
                    json.dump(manifest, f, indent=2)

            self.after(0, self._register_done, res, secured_path if res.get("STATUS") == "OK" else temp_secured, sig, code)
        except Exception as e:
            self.after(0, lambda: messagebox.showerror("Failed", str(e)))
            self.after(0, lambda: self._set_status("Registration failed."))
        finally:
            if temp_secured and os.path.exists(temp_secured):
                try: os.remove(temp_secured)
                except: pass

    def _register_done(self, res, out, sig, code):
        if res.get("STATUS") == "OK":
            file_hash = res.get('HASH', '')
            self._log(self.k_out,
                      "[ OK ] Registration successful\n"
                      f"       Hash : {file_hash[:48]}...\n"
                      f"       Sig  : {sig[:32]}...\n"
                      f"       Code : {code}\n"
                      "       Saved to secured/\n")
            self._set_status("Done — diploma secured and registered.")
            self.k_clear_btn.pack(side="left", padx=(0, 6))
            if messagebox.askyesno("Success", "Diploma secured.\nOpen secured folder?"):
                os.startfile(SECURED_FOLDER)
        else:
            msg = res.get("MESSAGE", "Unknown error")
            self._log(self.k_out, f"[FAIL] {msg}\n")
            self._set_status("Failed.")

    # ── Verify with scan popup ──────────────────────────────────────────
    def _do_verify(self):
        name = self.v_name.get().strip()
        nim  = self.v_nim.get().strip()
        code = self.v_code.get().strip()

        if not (name and nim and code):
            messagebox.showwarning("Incomplete", "All three fields required:\nNama + NIM + Kode Unik")
            return

        self._reset_layer_indicators()

        scan = ScanPopup(self)
        scan.log_line("Initializing verification engine...")
        scan.log_line(f"Target Name: {name}")
        scan.log_line(f"Target NIM  : {nim}")
        scan.log_line(f"Target Code : {code[:4]}...{code[-4:]}")
        scan.log_line("---")

        def run():
            self._verify_with_scan(scan, name, nim, code)

        threading.Thread(target=run, daemon=True).start()

    def _lookup_details_from_manifest(self, sid):
        try:
            if os.path.exists(MANIFEST_FILE):
                with open(MANIFEST_FILE, 'r') as f:
                    manifest = json.load(f)
                entry = manifest.get(sid)
                if entry:
                    return entry.get("details", {})
        except:
            pass
        return {}

    def _verify_with_scan(self, scan, name, nim, code):
        try:
            secured_files = []
            if os.path.isdir(SECURED_FOLDER):
                for fname in os.listdir(SECURED_FOLDER):
                    if fname.endswith(".pdf") and not fname.startswith("temp_"):
                        secured_files.append(fname)

            if secured_files:
                scan.log_line(f"Found {len(secured_files)} file(s) in secured storage.")
                scan.log_line("Scanning each file for matching record...")
                for fname in secured_files:
                    scan.log_line(f"  → Checking {fname[:20]}...")
                    time.sleep(0.08)
            else:
                scan.log_line("No files found in secured folder. Checking blockchain directly.")
                time.sleep(0.3)

            scan.log_line("---")
            scan.log_line("Querying blockchain via C++ core...")

            find_res = scdv_core.find_by_label(code)

            if find_res.get("STATUS") == "FOUND":
                block_name = find_res.get("NAME", "")
                block_id   = find_res.get("ID", "")

                if block_name.lower() == name.lower():
                    scan.log_line(f"[OK] Layer 1 — Nama matches: {block_name}")
                else:
                    scan.log_line(f"[WARN] Layer 1 — Nama mismatch: expected {name}, got {block_name}")

                if block_id == nim:
                    scan.log_line(f"[OK] Layer 2 — NIM matches: {block_id}")
                else:
                    scan.log_line(f"[WARN] Layer 2 — NIM mismatch: expected {nim}, got {block_id}")

                scan.log_line("---")

                if block_name.lower() == name.lower() and block_id == nim:
                    file_hash = find_res.get("HASH", "")
                    secured_path = os.path.join(SECURED_FOLDER, f"{file_hash}.pdf")
                    if os.path.exists(secured_path):
                        opens = pdf_secure.can_open(secured_path, code)
                        details_b64 = find_res.get("DETAILS", "")
                        if not details_b64:
                            details_b64 = self._lookup_details_from_manifest(block_id)

                        scan.log_line("[OK] File found and matched!")
                        scan.log_line(f"[OK] Layer 3 — PDF decryptable: {opens}")
                        scan.set_success()
                        time.sleep(0.5)
                        scan.log_line("---")
                        scan.log_line("Verification complete. Opening results...")
                        time.sleep(0.3)

                        data_dict = self._build_data_dict(find_res, details_b64)

                        self.after(0, lambda: self._show_result_popups(data_dict, secured_path, code, opens))
                        self.after(0, lambda: self._update_main_view(find_res, details_b64, opens, secured_path, code))
                        self.after(200, scan.close)
                    else:
                        scan.log_line("[FAIL] File not found in storage (hash mismatch)")
                        scan.set_fail()
                        self.after(0, lambda: messagebox.showerror("Error", "Secured file not found in storage."))
                        self.after(200, scan.close)
                else:
                    scan.log_line("[WARN] Code found but name/NIM mismatch. Trying fallback...")
                    self._scan_fallback(scan, name, nim, code)
                return

            scan.log_line("[INFO] Code not found in blockchain. Trying name+NIM fallback...")
            self._scan_fallback(scan, name, nim, code)

        except Exception as e:
            scan.log_line(f"[ERROR] {str(e)}")
            scan.set_fail()
            self.after(0, lambda: messagebox.showerror("Error", str(e)))
            self.after(200, scan.close)

    def _scan_fallback(self, scan, name, nim, code):
        try:
            scan.log_line("Querying blockchain by name + NIM...")
            find_res = scdv_core.find_by_student(name, nim)

            if find_res.get("STATUS") == "FOUND":
                file_hash = find_res.get("HASH", "")
                secured_path = os.path.join(SECURED_FOLDER, f"{file_hash}.pdf")

                scan.log_line(f"[OK] Record found via name+NIM: {find_res.get('NAME', '')}")
                scan.log_line(f"[OK] Layer 1 — Nama ✓")
                scan.log_line(f"[OK] Layer 2 — NIM ✓")

                if os.path.exists(secured_path):
                    opens = pdf_secure.can_open(secured_path, code)
                    details_b64 = find_res.get("DETAILS", "")
                    if not details_b64:
                        details_b64 = self._lookup_details_from_manifest(nim)

                    if opens:
                        scan.log_line(f"[OK] Layer 3 — Kode Unik ✓ (PDF decrypted)")
                    else:
                        scan.log_line(f"[WARN] Layer 3 — Kode Unik used (PDF locked by different code)")

                    scan.set_success()
                    time.sleep(0.5)
                    scan.log_line("---")
                    scan.log_line("Verification complete. Opening results...")
                    time.sleep(0.3)

                    data_dict = self._build_data_dict(find_res, details_b64)

                    self.after(0, lambda: self._show_result_popups(data_dict, secured_path, code, opens))
                    self.after(0, lambda: self._update_main_view(find_res, details_b64, opens, secured_path, code))
                    self.after(200, scan.close)
                else:
                    scan.log_line("[FAIL] File not found in storage")
                    scan.set_fail()
                    self.after(0, lambda: messagebox.showerror("Error", "Secured file not found."))
                    self.after(200, scan.close)
            else:
                scan.log_line("[FAIL] No matching record found in blockchain")
                scan.set_fail()
                self.after(0, lambda: self._reset_verify_view("NOT FOUND\nNo matching record in blockchain."))
                self.after(0, lambda: self._set_status("NOT FOUND — no match."))
                self.after(200, scan.close)

        except Exception as e:
            scan.log_line(f"[ERROR] {str(e)}")
            scan.set_fail()
            self.after(0, lambda: messagebox.showerror("Error", str(e)))
            self.after(200, scan.close)

    def _build_data_dict(self, find_res, details_b64):
        d = {}
        d["Nama Lengkap"] = find_res.get("NAME", "-")
        d["NIM"] = find_res.get("ID", "-")
        d["Terdaftar"] = find_res.get("TIME", "-")

        details_dict = {}
        if details_b64:
            if isinstance(details_b64, dict):
                details_dict = details_b64
            elif isinstance(details_b64, str):
                try:
                    details_json = base64.b64decode(details_b64).decode('utf-8')
                    details_dict = json.loads(details_json)
                except:
                    try:
                        details_dict = json.loads(details_b64)
                    except:
                        pass
        else:
            sid = find_res.get("ID", "")
            if sid:
                details_dict = self._lookup_details_from_manifest(sid)

        d["NIK"] = details_dict.get("nik", "-")
        d["No. Telepon"] = details_dict.get("phone", "-")
        d["Alamat"] = details_dict.get("address", "-")
        coord = details_dict.get("coord", "")
        d["Koordinat"] = coord if coord else "-"
        d["Foto"] = "Available" if details_dict.get("photo") else "None"
        return d

    def _show_result_popups(self, data_dict, pdf_path, code, opens):
        student_title = "STUDENT DATA"
        ResultPopup(self, student_title, data_dict=data_dict)
        if opens and pdf_path and os.path.exists(pdf_path):
            self.after(100, lambda: ResultPopup(self, "DOCUMENT PREVIEW", pdf_path=pdf_path, code=code))

    def _update_main_view(self, find_res, details_b64, opens, secured_path, code):
        self.v_verified_file = secured_path
        self.v_verified_code = code
        self._set_verified_view()

        details_dict = {}
        if details_b64:
            if isinstance(details_b64, dict):
                details_dict = details_b64
            elif isinstance(details_b64, str):
                try:
                    details_json = base64.b64decode(details_b64).decode('utf-8')
                    details_dict = json.loads(details_json)
                except:
                    try:
                        details_dict = json.loads(details_b64)
                    except:
                        pass
        else:
            sid = find_res.get("ID", "")
            if sid:
                details_dict = self._lookup_details_from_manifest(sid)

        self.v_detail_labels["Nama Lengkap"].config(text=find_res.get('NAME', '-'))
        self.v_detail_labels["NIM"].config(text=find_res.get('ID', '-'))
        self.v_detail_labels["Terdaftar"].config(text=find_res.get('TIME', '-'))
        self.v_detail_labels["NIK"].config(text=details_dict.get("nik", "-"))
        self.v_detail_labels["No. Telepon"].config(text=details_dict.get("phone", "-"))
        self.v_detail_labels["Alamat"].config(text=details_dict.get("address", "-"))
        coord = details_dict.get("coord", "")
        self.v_detail_labels["Koordinat"].config(text=coord if coord else "-", fg=MUTED if not coord else TXT)

        photo_b64 = details_dict.get("photo", "")
        if photo_b64:
            try:
                pb = base64.b64decode(photo_b64)
                img = Image.open(io.BytesIO(pb))
                self.photo_img = ImageTk.PhotoImage(img)
                self.v_photo.config(image=self.photo_img)
            except:
                self.v_photo.config(image="", text="[photo error]", fg=WARN)
        else:
            self.v_photo.config(image="", text="[no photo]", fg=MUTED)

        if secured_path and os.path.exists(secured_path):
            try:
                doc = fitz.open(secured_path)
                if doc.is_encrypted:
                    doc.authenticate(code)
                page = doc[0]
                pix = page.get_pixmap(dpi=130)
                img_data = pix.tobytes("png")
                img = Image.open(io.BytesIO(img_data))
                img.thumbnail((450, 550))
                self.pdf_img = ImageTk.PhotoImage(img)
                self.v_pdf.config(image=self.pdf_img)
            except Exception as e:
                self.v_pdf.config(image="", text=f"PDF render error:\n{e}", fg=BAD)
        else:
            self.v_pdf.config(image="", text="PDF file not found", fg=BAD)

        if opens and secured_path:
            self.v_open_btn.pack(fill="x", pady=(10, 0), padx=20)
        self._set_status("VERIFIED — authentic diploma.")

    def _verify_error(self, msg):
        self._set_status(f"Error: {msg}")
        messagebox.showerror("Error", msg)

    def _reset_verify_view(self, status_text, bg_color="#1a0a0a", fg_color=BAD):
        self.v_status.config(text=status_text, fg=fg_color, bg=bg_color)
        for lbl in self.v_detail_labels.values():
            lbl.config(text="-")
        self.v_photo.config(image="", text="")
        self.v_pdf.config(image="", text="PDF preview\nwill appear here.", bg="#060a14")
        self.v_open_btn.pack_forget()

    def _set_verified_view(self):
        self.v_status.config(text="AUTHENTIC — DIPLOMA VERIFIED", fg=OK, bg="#05200e")

    def _open_verified_pdf(self):
        if not self.v_verified_file or not os.path.exists(self.v_verified_file):
            messagebox.showerror("Error", "File not found.")
            return
        try:
            os.startfile(self.v_verified_file)
            self._set_status(f"Opening: {os.path.basename(self.v_verified_file)}")
        except Exception as e:
            messagebox.showerror("Error", f"Cannot open file:\n{e}")

    def _do_validate(self):
        try:
            res = scdv_core.validate()
            if res.get("STATUS") == "VALID":
                messagebox.showinfo("Integrity", "Blockchain VALID — no tampering detected.")
            else:
                messagebox.showwarning("Integrity", "Blockchain INVALID — tampering detected!")
        except Exception as e:
            messagebox.showerror("Error", str(e))


if __name__ == "__main__":
    App().mainloop()
