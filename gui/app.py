import os, json, threading, base64, io, tkinter as tk
from tkinter import ttk, filedialog, messagebox
from datetime import datetime
import shutil
import urllib.request, urllib.parse
import time
import math

from PIL import Image, ImageTk
import fitz

import pdf_secure
import scdv_core

SECURED_FOLDER = os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))), "secured")
MANIFEST_FILE  = os.path.join(SECURED_FOLDER, "manifest.json")

# ─── PREMIUM PALETTE ─────────────────────────────────────────────────────────
BG       = "#08090f"
SURFACE  = "#0d1117"
CARD     = "#111520"
CARD2    = "#161c2d"
BORDER   = "#1e2a45"
ACCENT   = "#c9a84c"
ACCENT2  = "#e8c96a"
ACCENT3  = "#7c6120"
EMERALD  = "#10b981"
CRIMSON  = "#e53e3e"
AMBER    = "#f59e0b"
SAPPHIRE = "#3b82f6"
TXT      = "#e8eaf0"
TXT2     = "#9ca3b0"
MUTED    = "#4a5568"
WHITE    = "#ffffff"


def geocode_address(address):
    try:
        url = "https://nominatim.openstreetmap.org/search?" + urllib.parse.urlencode(
            {"q": address, "format": "json", "limit": 1})
        req = urllib.request.Request(url, headers={"User-Agent": "SecureChainDiplomaVerifier/1.0"})
        with urllib.request.urlopen(req, timeout=5) as resp:
            data = json.loads(resp.read().decode())
            if data:
                return {"lat": data[0]["lat"], "lon": data[0]["lon"]}
    except:
        pass
    return None


# ─── SCROLL FRAME ────────────────────────────────────────────────────────────
class ScrollFrame(tk.Frame):
    def __init__(self, parent, bgcolor=SURFACE):
        super().__init__(parent, bg=bgcolor)
        self.canvas = tk.Canvas(self, bg=bgcolor, highlightthickness=0, bd=0)
        self.scrollbar = ttk.Scrollbar(self, orient="vertical", command=self.canvas.yview)
        self.scroll_frame = tk.Frame(self.canvas, bg=bgcolor)
        self.scroll_frame.bind("<Configure>",
                               lambda e: self.canvas.configure(
                                   scrollregion=self.canvas.bbox("all")))
        self.canvas.create_window((0, 0), window=self.scroll_frame, anchor="nw",
                                  width=self.canvas.winfo_reqwidth() or 440)
        self.canvas.configure(yscrollcommand=self.scrollbar.set)
        self.canvas.pack(side="left", fill="both", expand=True)
        self.scrollbar.pack(side="right", fill="y")
        self.bind("<Enter>", self._bind_mouse)
        self.bind("<Leave>", self._unbind_mouse)
        self.bind("<Configure>", self._resize_width)

    def _resize_width(self, event=None):
        w = self.canvas.winfo_width() - 20
        if w > 100:
            self.canvas.itemconfig(1, width=w)

    def _bind_mouse(self, event=None):
        self.canvas.bind_all("<MouseWheel>", self._on_mousewheel, add="+")

    def _unbind_mouse(self, event=None):
        self.canvas.unbind_all("<MouseWheel>")

    def _on_mousewheel(self, event):
        self.canvas.yview_scroll(-1 * (event.delta // 120), "units")


# ─── SCAN POPUP — 3 TERMINAL WINDOWS ────────────────────────────────────────
class ScanPopup:
    """
    Spawns 3 separate non-blocking terminal-style windows:
      W1 – File Scanner (gold)     W2 – Blockchain Matcher (emerald)
      W3 – Status / Control (blue)
    Windows stay open after scan; user closes manually via button.
    """
    def __init__(self, parent):
        self.parent      = parent
        self._animating  = True
        self._step       = 0
        self._blink_on   = True
        self._spin_chars = ["◐", "◓", "◑", "◒"]
        self._windows    = []
        self._prog_val   = 0

        # Calculate positions: 3 windows side by side, centred on screen
        sw, sh  = parent.winfo_screenwidth(), parent.winfo_screenheight()
        win_w, win_h = 370, 310
        gap = 8
        total_w = win_w * 3 + gap * 2
        x0 = max(0, (sw - total_w) // 2)
        y0 = max(60, (sh - win_h) // 2)

        # ── W1: File Scanner ─────────────────────────────────────────────
        self.w1 = self._make_win(parent, "① File Scanner",
                                 f"{win_w}x{win_h}+{x0}+{y0}", ACCENT)
        h1 = self._win_header(self.w1, "  ① FILE SCANNER", ACCENT)
        self._led1 = tk.Canvas(h1, width=12, height=12, bg=CARD2,
                               highlightthickness=0)
        self._led1.pack(side="right", padx=10)
        self._dot1 = self._led1.create_oval(2, 2, 10, 10, fill=AMBER, outline="")
        self.left_log = self._make_log(self.w1, "#88b4d0")
        self.left_log.insert("end", "[FILE] Initializing filesystem scan...\n")

        # ── W2: Blockchain Matcher ───────────────────────────────────────
        self.w2 = self._make_win(parent, "② Blockchain Matcher",
                                 f"{win_w}x{win_h}+{x0+win_w+gap}+{y0}", EMERALD)
        h2 = self._win_header(self.w2, "  ② BLOCKCHAIN MATCHER", EMERALD)
        self._led2 = tk.Canvas(h2, width=12, height=12, bg=CARD2,
                               highlightthickness=0)
        self._led2.pack(side="right", padx=10)
        self._dot2 = self._led2.create_oval(2, 2, 10, 10, fill=AMBER, outline="")
        self.right_log = self._make_log(self.w2, "#7dc98c")
        self.right_log.insert("end", "[CHAIN] Connecting to ledger...\n")

        # ── W3: Status / Control ─────────────────────────────────────────
        self.w3 = self._make_win(parent, "③ Verification Status",
                                 f"{win_w}x{win_h}+{x0+(win_w+gap)*2}+{y0}", SAPPHIRE)
        h3 = self._win_header(self.w3, "  ③ STATUS MONITOR", SAPPHIRE)
        self._spinner_lbl = tk.Label(h3, text="◐", bg=CARD2, fg=ACCENT2,
                                     font=("Consolas", 10))
        self._spinner_lbl.pack(side="right", padx=10)

        self._status_log = self._make_log(self.w3, TXT, height=5)
        self._status_log.insert("end", "[SYS] Verification engine started\n")
        self._status_log.insert("end", "[SYS] Awaiting scan results...\n")

        # Progress bar
        self._prog = tk.Canvas(self.w3, bg=CARD2, height=4, highlightthickness=0)
        self._prog.pack(fill="x", padx=6, pady=3)
        self._prog_val = 0

        # Big result label
        self._result_lbl = tk.Label(self.w3, text="SCANNING...",
                                    bg=CARD, fg=AMBER,
                                    font=("Segoe UI Semibold", 11), pady=6)
        self._result_lbl.pack(fill="x", padx=6, pady=(2, 4))

        # Close-all button (always visible)
        self._close_btn = tk.Button(self.w3, text="✕  Tutup Semua Window",
                                    command=self.close_all,
                                    bg=CARD2, fg=MUTED, relief="flat",
                                    font=("Segoe UI", 8), pady=5,
                                    cursor="hand2",
                                    activebackground=CRIMSON,
                                    activeforeground=WHITE)
        self._close_btn.pack(fill="x", padx=6, pady=(0, 6))

        # Lightweight animations
        self._blink_state = True
        self._do_blink()
        self._do_spin()
        self._do_progress()

    # ── Builder helpers ──────────────────────────────────────────────────────
    def _make_win(self, parent, title, geometry, accent):
        w = tk.Toplevel(parent)
        w.title(title)
        w.geometry(geometry)
        w.configure(bg=BG)
        w.resizable(True, True)
        w.transient(parent)
        # NO grab_set — windows are non-blocking
        tk.Frame(w, bg=accent, height=3).pack(fill="x")
        self._windows.append(w)
        return w

    def _win_header(self, win, label_text, fg_color):
        hdr = tk.Frame(win, bg=CARD2, pady=7)
        hdr.pack(fill="x")
        tk.Label(hdr, text=label_text, bg=CARD2, fg=fg_color,
                 font=("Consolas", 9, "bold")).pack(side="left")
        return hdr

    def _make_log(self, win, fg, height=None):
        kw = dict(bg="#060b12", fg=fg, font=("Consolas", 8),
                  wrap="word", padx=8, pady=5, bd=0,
                  highlightthickness=0, state="normal")
        if height:
            kw["height"] = height
        t = tk.Text(win, **kw)
        t.pack(fill="both", expand=True, padx=2, pady=(0, 2))
        return t

    # ── Animations (no math import needed, just toggle) ──────────────────────
    def _do_blink(self):
        if not self._animating:
            return
        self._blink_state = not self._blink_state
        col = AMBER if self._blink_state else "#2a1f00"
        try:
            self._led1.itemconfig(self._dot1, fill=col)
            self._led2.itemconfig(self._dot2, fill=col)
            self.w1.after(480, self._do_blink)
        except:
            pass

    def _do_spin(self):
        if not self._animating:
            return
        self._step = (self._step + 1) % 4
        try:
            self._spinner_lbl.config(text=self._spin_chars[self._step])
            self.w3.after(260, self._do_spin)
        except:
            pass

    def _do_progress(self):
        if not self._animating:
            return
        try:
            w = self._prog.winfo_width() or 340
            self._prog_val = (self._prog_val + 7) % (w + 100)
            self._prog.delete("bar")
            bw = min(110, w)
            x1 = self._prog_val - bw
            x2 = self._prog_val
            self._prog.create_rectangle(max(0, x1), 0, min(w, x2), 4,
                                        fill=ACCENT, outline="", tags="bar")
            self.w3.after(40, self._do_progress)
        except:
            pass

    # ── Log writers ──────────────────────────────────────────────────────────
    def _insert(self, widget, text):
        try:
            widget.insert("end", f"[{datetime.now().strftime('%H:%M:%S')}] {text}\n")
            widget.see("end")
        except:
            pass

    def log_left(self, text):
        try:
            self.w1.after(0, lambda t=text: self._insert(self.left_log, t))
        except:
            pass

    def log_right(self, text):
        try:
            self.w2.after(0, lambda t=text: self._insert(self.right_log, t))
        except:
            pass

    def log_status(self, text):
        try:
            self.w3.after(0, lambda t=text: self._insert(self._status_log, t))
        except:
            pass

    # ── Final state — windows STAY open ─────────────────────────────────────
    def set_success(self):
        self._animating = False
        def _apply():
            try:
                self._led1.itemconfig(self._dot1, fill=EMERALD)
                self._led2.itemconfig(self._dot2, fill=EMERALD)
                self._spinner_lbl.config(text="✓", fg=EMERALD)
                self._result_lbl.config(text="✓  VERIFIED — VALID",
                                        fg=EMERALD, bg="#061a0e")
                pw = self._prog.winfo_width() or 340
                self._prog.delete("bar")
                self._prog.create_rectangle(0, 0, pw, 4,
                                            fill=EMERALD, outline="", tags="bar")
                self._insert(self._status_log, "✓ Verification PASSED")
                self._close_btn.config(fg=TXT)
            except:
                pass
        try:
            self.w3.after(0, _apply)
        except:
            pass

    def set_fail(self):
        self._animating = False
        def _apply():
            try:
                self._led1.itemconfig(self._dot1, fill=CRIMSON)
                self._led2.itemconfig(self._dot2, fill=CRIMSON)
                self._spinner_lbl.config(text="✗", fg=CRIMSON)
                self._result_lbl.config(text="✗  FAILED — INVALID",
                                        fg=CRIMSON, bg="#1a0606")
                pw = self._prog.winfo_width() or 340
                self._prog.delete("bar")
                self._prog.create_rectangle(0, 0, pw, 4,
                                            fill=CRIMSON, outline="", tags="bar")
                self._insert(self._status_log, "✗ Verification FAILED")
            except:
                pass
        try:
            self.w3.after(0, _apply)
        except:
            pass

    def close(self):
        """Called by verify logic. Windows stay open — user reads logs then closes."""
        self._animating = False

    def close_all(self):
        self._animating = False
        for w in self._windows:
            try:
                if w.winfo_exists():
                    w.destroy()
            except:
                pass


# ─── RESULT POPUP (PREMIUM) ──────────────────────────────────────────────────
class ResultPopup:
    def __init__(self, parent, title, data_dict=None, pdf_path=None, code=None, opens=True):
        self.win = tk.Toplevel(parent)
        self.win.title("Credential Verification")
        self.win.geometry("1020x620")
        self.win.configure(bg=BG)
        self.win.resizable(False, False)
        self.win.transient(parent)
        self.win.grab_set()

        accent_color = EMERALD if opens else CRIMSON
        tk.Frame(self.win, bg=accent_color, height=3).pack(fill="x")

        hdr = tk.Frame(self.win, bg=CARD2, pady=14)
        hdr.pack(fill="x")
        tk.Frame(hdr, bg=accent_color, width=4).pack(side="left", fill="y")
        hdr_inner = tk.Frame(hdr, bg=CARD2)
        hdr_inner.pack(side="left", padx=18, fill="x", expand=True)
        tk.Label(hdr_inner, text="CREDENTIAL VERIFICATION RESULT",
                 bg=CARD2, fg=accent_color,
                 font=("Segoe UI", 11, "bold")).pack(anchor="w")
        tk.Label(hdr_inner,
                 text="SecureChain  \u00b7  Blockchain Ledger  \u00b7  AES-256 Encrypted",
                 bg=CARD2, fg=TXT2, font=("Segoe UI", 8)).pack(anchor="w")
        close = tk.Label(hdr, text="  \u2715  ", bg=CARD2, fg=MUTED,
                         font=("Segoe UI", 12), cursor="hand2")
        close.pack(side="right", padx=10)
        close.bind("<Button-1>", lambda e: self.win.destroy())
        close.bind("<Enter>", lambda e: close.config(fg=CRIMSON))
        close.bind("<Leave>", lambda e: close.config(fg=MUTED))

        tk.Frame(self.win, height=1, bg=BORDER).pack(fill="x")

        body = tk.Frame(self.win, bg=BG)
        body.pack(fill="both", expand=True, padx=18, pady=14)

        # Left col: Photo
        left_col = tk.Frame(body, bg=BG, width=170)
        left_col.pack(side="left", fill="y", padx=(0, 18))
        left_col.pack_propagate(False)

        photo_outer = tk.Frame(left_col, bg=ACCENT, padx=2, pady=2)
        photo_outer.pack(pady=(30, 0))
        photo_inner = tk.Frame(photo_outer, bg=CARD)
        photo_inner.pack()

        self.photo_lbl = tk.Label(photo_inner, bg=CARD, width=20, height=12)
        self.photo_lbl.pack()

        photo_b64 = data_dict.get("photo_b64", "") if data_dict else ""
        if photo_b64 and photo_b64 != "None":
            try:
                pb  = base64.b64decode(photo_b64)
                img = Image.open(io.BytesIO(pb))
                img = img.resize((162, 200), Image.Resampling.LANCZOS)
                self.photo_img = ImageTk.PhotoImage(img)
                self.photo_lbl.config(image=self.photo_img, width=162, height=200)
            except:
                self.photo_lbl.config(text="NO\nPHOTO", fg=MUTED, font=("Segoe UI", 9))
        else:
            self.photo_lbl.config(text="NO\nPHOTO", fg=MUTED, font=("Segoe UI", 9))

        badge_text = "\u2713  VERIFIED" if opens else "\u2717  INVALID"
        tk.Label(left_col, text=badge_text, bg=accent_color, fg=WHITE,
                 font=("Segoe UI Semibold", 9), pady=5).pack(fill="x", pady=(10, 0))

        # Middle col: Details
        mid_col = tk.Frame(body, bg=BG)
        mid_col.pack(side="left", fill="both", expand=True)

        name_val = data_dict.get("Nama Lengkap", "-") if data_dict else "-"
        tk.Label(mid_col, text=str(name_val).upper(), bg=BG, fg=WHITE,
                 font=("Segoe UI Semibold", 16), anchor="w").pack(fill="x")

        nim_val = data_dict.get("NIM", "-") if data_dict else "-"
        nim_row = tk.Frame(mid_col, bg=BG)
        nim_row.pack(fill="x", pady=(2, 12))
        tk.Label(nim_row, text="NIM", bg=BG, fg=MUTED,
                 font=("Segoe UI", 8)).pack(side="left")
        tk.Label(nim_row, text=f"  {nim_val}", bg=BG, fg=ACCENT,
                 font=("Segoe UI Semibold", 9)).pack(side="left")

        tk.Frame(mid_col, height=1, bg=BORDER).pack(fill="x", pady=(0, 10))

        if data_dict:
            fields = [
                ("\U0001f3db  Nama Kampus",    data_dict.get("Nama Kampus",   "-")),
                ("\U0001f4da  Program Studi",  data_dict.get("Program Studi", "-")),
                ("\U0001f393  Jenjang",        data_dict.get("Jenjang",       "-")),
                ("\U0001f4c5  Angkatan",       data_dict.get("Angkatan",      "-")),
                ("\U0001faba  NIK",            data_dict.get("NIK",           "-")),
                ("\U0001f4de  No. Telepon",    data_dict.get("No. Telepon",   "-")),
                ("\U0001f4cd  Alamat",         data_dict.get("Alamat",        "-")),
                ("\U0001f5fa  Koordinat GPS",  data_dict.get("Koordinat",     "-")),
                ("\U0001f550  Terdaftar",      data_dict.get("Terdaftar",     "-")),
            ]
        else:
            fields = []

        grid_f = tk.Frame(mid_col, bg=BG)
        grid_f.pack(fill="both", expand=True)

        for i, (lbl, val) in enumerate(fields):
            row_bg = "#0e1322" if i % 2 == 0 else BG
            row = tk.Frame(grid_f, bg=row_bg)
            row.pack(fill="x", ipady=4)
            tk.Label(row, text=f"  {lbl}", bg=row_bg, fg=TXT2,
                     width=20, anchor="w", font=("Segoe UI", 8, "bold")).pack(side="left")
            tk.Label(row, text="\u2502", bg=row_bg, fg=BORDER,
                     font=("Consolas", 9)).pack(side="left")
            fg_v = WHITE if (val and val != "-") else MUTED
            tk.Label(row, text=f"  {val}" if val else "  \u2014", bg=row_bg, fg=fg_v,
                     anchor="w", font=("Segoe UI", 9),
                     wraplength=320, justify="left").pack(
                side="left", fill="x", expand=True, padx=(0, 8))

        status_bg = "#061a0e" if opens else "#1a0606"
        status_tx = ("  \u25c9  STATUS: TERDEKRIPSI   |   BLOCKCHAIN: VALID"
                     if opens else
                     "  \u25c9  STATUS: GAGAL   |   BLOCKCHAIN: TIDAK VALID")
        tk.Frame(mid_col, height=1, bg=BORDER).pack(fill="x", pady=(8, 4))
        tk.Label(mid_col, text=status_tx, bg=status_bg, fg=accent_color,
                 font=("Segoe UI Semibold", 8), anchor="w",
                 padx=10, pady=6).pack(fill="x")

        # Right col: PDF Preview
        right_col = tk.Frame(body, bg=BG, width=260)
        right_col.pack(side="right", fill="y", padx=(18, 0))
        right_col.pack_propagate(False)

        tk.Label(right_col, text="DOCUMENT PREVIEW", bg=BG, fg=TXT2,
                 font=("Segoe UI", 8, "bold")).pack(anchor="w", pady=(0, 6))

        preview_outer = tk.Frame(right_col, bg=ACCENT, padx=1, pady=1)
        preview_outer.pack(fill="both", expand=True)
        preview_box = tk.Frame(preview_outer, bg=CARD)
        preview_box.pack(fill="both", expand=True)

        self.pdf_lbl = tk.Label(preview_box, bg=CARD, text="LOADING...", fg=MUTED,
                                font=("Segoe UI", 9))
        self.pdf_lbl.pack(fill="both", expand=True, padx=4, pady=4)

        if pdf_path and os.path.exists(pdf_path):
            try:
                doc = fitz.open(pdf_path)
                if doc.is_encrypted:
                    doc.authenticate(code)
                page = doc[0]
                pix  = page.get_pixmap(dpi=96)
                img  = Image.open(io.BytesIO(pix.tobytes("png")))
                img.thumbnail((250, 330))
                self.pdf_img = ImageTk.PhotoImage(img)
                self.pdf_lbl.config(image=self.pdf_img, text="")
            except Exception as e:
                self.pdf_lbl.config(text=f"RENDER ERROR\n{e}", fg=CRIMSON)
        else:
            self.pdf_lbl.config(text="FILE NOT\nFOUND", fg=CRIMSON)

        if opens and pdf_path:
            tk.Button(right_col, text="\u25b6  OPEN DOCUMENT",
                      command=lambda: os.startfile(pdf_path),
                      bg=EMERALD, fg="#08090f", relief="flat",
                      font=("Segoe UI Semibold", 9), pady=7,
                      cursor="hand2", activebackground="#0ea875").pack(
                fill="x", pady=(8, 0))


# ═══════════════════════════════════════════════════════════════════════════════
# MAIN APPLICATION
# ═══════════════════════════════════════════════════════════════════════════════
class App(tk.Tk):
    def __init__(self):
        super().__init__()
        self.title("SecureChain \u2014 Blockchain Credential System")
        self.geometry("1160x800")
        self.configure(bg=BG)
        self.minsize(980, 680)

        style = ttk.Style(self)
        style.theme_use("default")
        style.configure("TScrollbar", background=CARD2, troughcolor=SURFACE,
                        bordercolor=BORDER, arrowcolor=TXT2)

        self._build_sidebar()
        self._build_main()
        self._build_statusbar()

    # ─── SIDEBAR ──────────────────────────────────────────────────────────────
    def _build_sidebar(self):
        self.sidebar = tk.Frame(self, bg=CARD2, width=224)
        self.sidebar.pack(side="left", fill="y")
        self.sidebar.pack_propagate(False)

        # Logo
        logo_frame = tk.Frame(self.sidebar, bg="#0a0e1c", pady=22)
        logo_frame.pack(fill="x")

        shield = tk.Canvas(logo_frame, width=48, height=52, bg="#0a0e1c",
                           highlightthickness=0)
        shield.pack()
        pts = [24, 2, 46, 10, 46, 28, 24, 50, 2, 28, 2, 10]
        shield.create_polygon(pts, fill=ACCENT3, outline=ACCENT, width=2)
        shield.create_text(24, 26, text="SC", fill=ACCENT2,
                           font=("Segoe UI Semibold", 11, "bold"))

        tk.Label(logo_frame, text="SECURECHAIN", bg="#0a0e1c", fg=ACCENT,
                 font=("Segoe UI", 9, "bold")).pack(pady=(8, 0))
        tk.Label(logo_frame, text="Credential Verification System", bg="#0a0e1c",
                 fg=MUTED, font=("Segoe UI", 7)).pack()

        tk.Frame(self.sidebar, height=1, bg=BORDER).pack(fill="x")

        tk.Label(self.sidebar, text="NAVIGATION", bg=CARD2, fg=MUTED,
                 font=("Segoe UI", 7, "bold"), anchor="w",
                 padx=18).pack(fill="x", pady=(16, 6))

        self._nav_buttons = {}
        nav_items = [
            ("upload", "\u2b06  Upload & Register", "Register new diploma credential"),
            ("verify", "\u29c6  Verify Credential",  "Authenticate via blockchain"),
        ]

        for key, label, sub in nav_items:
            btn_frame = tk.Frame(self.sidebar, bg=CARD2, cursor="hand2")
            btn_frame.pack(fill="x", pady=1)
            indicator = tk.Frame(btn_frame, bg=CARD2, width=3)
            indicator.pack(side="left", fill="y")
            inner = tk.Frame(btn_frame, bg=CARD2, pady=10, padx=14)
            inner.pack(side="left", fill="x", expand=True)
            lbl = tk.Label(inner, text=label, bg=CARD2, fg=TXT2,
                           font=("Segoe UI", 10), anchor="w")
            lbl.pack(fill="x")
            sub_lbl = tk.Label(inner, text=sub, bg=CARD2, fg=MUTED,
                               font=("Segoe UI", 7), anchor="w")
            sub_lbl.pack(fill="x")
            self._nav_buttons[key] = (btn_frame, indicator, inner, lbl, sub_lbl)

            def make_click(k):
                def handler(e=None):
                    self._select_nav(k)
                return handler

            def make_enter(k):
                return lambda e: self._hover_nav(k)

            def make_leave(k):
                return lambda e: self._unhover_nav(k)

            for widget in (btn_frame, indicator, inner, lbl, sub_lbl):
                widget.bind("<Button-1>", make_click(key))
                widget.bind("<Enter>",    make_enter(key))
                widget.bind("<Leave>",    make_leave(key))

        # Tools section
        tk.Frame(self.sidebar, height=1, bg=BORDER).pack(fill="x", pady=(16, 0))
        tk.Label(self.sidebar, text="TOOLS", bg=CARD2, fg=MUTED,
                 font=("Segoe UI", 7, "bold"), anchor="w",
                 padx=18).pack(fill="x", pady=(10, 6))

        int_btn = tk.Frame(self.sidebar, bg=CARD2, cursor="hand2")
        int_btn.pack(fill="x", pady=1)
        tk.Frame(int_btn, bg=CARD2, width=3).pack(side="left", fill="y")
        int_inner = tk.Frame(int_btn, bg=CARD2, pady=8, padx=14)
        int_inner.pack(side="left", fill="x", expand=True)
        int_lbl = tk.Label(int_inner, text="\u25c8  Chain Integrity",
                           bg=CARD2, fg=TXT2, font=("Segoe UI", 10), anchor="w")
        int_lbl.pack(fill="x")
        tk.Label(int_inner, text="Validate entire ledger",
                 bg=CARD2, fg=MUTED, font=("Segoe UI", 7), anchor="w").pack(fill="x")

        for w in (int_btn, int_inner, int_lbl):
            w.bind("<Button-1>", lambda e: self._do_validate())

        def _int_enter(e):
            int_inner.config(bg="#1a2235")
            for child in int_inner.winfo_children():
                try: child.config(bg="#1a2235")
                except: pass

        def _int_leave(e):
            int_inner.config(bg=CARD2)
            for child in int_inner.winfo_children():
                try: child.config(bg=CARD2)
                except: pass

        for w in (int_btn, int_inner, int_lbl):
            w.bind("<Enter>", _int_enter)
            w.bind("<Leave>", _int_leave)

        tk.Frame(self.sidebar, bg=CARD2).pack(fill="y", expand=True)
        tk.Frame(self.sidebar, height=1, bg=BORDER).pack(fill="x")
        ver_f = tk.Frame(self.sidebar, bg=CARD2, pady=14)
        ver_f.pack(fill="x")
        tk.Label(ver_f, text="v 1.0  \u00b7  Production Build",
                 bg=CARD2, fg=MUTED, font=("Segoe UI", 7)).pack()
        tk.Label(ver_f, text="C++ Core  \u00b7  OpenSSL  \u00b7  SHA-256",
                 bg=CARD2, fg=MUTED, font=("Segoe UI", 7)).pack()

        self._current_nav = None
        self._select_nav("upload")

    def _hover_nav(self, key):
        if key == self._current_nav:
            return
        b, ind, inner, lbl, sub = self._nav_buttons[key]
        for w in (b, inner): w.config(bg="#1a2235")
        lbl.config(bg="#1a2235", fg=TXT)
        sub.config(bg="#1a2235")
        ind.config(bg="#1a2235")

    def _unhover_nav(self, key):
        if key == self._current_nav:
            return
        b, ind, inner, lbl, sub = self._nav_buttons[key]
        for w in (b, inner): w.config(bg=CARD2)
        lbl.config(bg=CARD2, fg=TXT2)
        sub.config(bg=CARD2)
        ind.config(bg=CARD2)

    def _select_nav(self, key):
        if self._current_nav and self._current_nav in self._nav_buttons:
            b, ind, inner, lbl, sub = self._nav_buttons[self._current_nav]
            for w in (b, inner): w.config(bg=CARD2)
            lbl.config(bg=CARD2, fg=TXT2)
            sub.config(bg=CARD2)
            ind.config(bg=CARD2)

        self._current_nav = key
        b, ind, inner, lbl, sub = self._nav_buttons[key]
        for w in (b, inner): w.config(bg="#161d30")
        lbl.config(bg="#161d30", fg=ACCENT)
        sub.config(bg="#161d30")
        ind.config(bg=ACCENT)

        titles = {
            "upload": ("Upload & Register",      "Register & secure diploma credentials"),
            "verify": ("Verify Credential",       "Authenticate via blockchain ledger"),
        }
        if hasattr(self, "_page_title"):
            t, s = titles.get(key, ("", ""))
            self._page_title.config(text=t)
            self._page_sub.config(text=s)
        if hasattr(self, "_main_pages"):
            self._main_pages[key].lift()

    # ─── MAIN AREA ────────────────────────────────────────────────────────────
    def _build_main(self):
        main_area = tk.Frame(self, bg=BG)
        main_area.pack(side="left", fill="both", expand=True)

        # Top bar
        topbar = tk.Frame(main_area, bg=SURFACE, height=56)
        topbar.pack(fill="x")
        topbar.pack_propagate(False)
        tk.Frame(topbar, height=2, bg=ACCENT).pack(fill="x", side="bottom")

        title_frame = tk.Frame(topbar, bg=SURFACE)
        title_frame.pack(side="left", padx=24, pady=10)
        self._page_title = tk.Label(title_frame, text="Upload & Register",
                                    bg=SURFACE, fg=WHITE,
                                    font=("Segoe UI Semibold", 14))
        self._page_title.pack(anchor="w")
        self._page_sub = tk.Label(title_frame,
                                  text="Register & secure diploma credentials",
                                  bg=SURFACE, fg=TXT2, font=("Segoe UI", 8))
        self._page_sub.pack(anchor="w")

        right_bar = tk.Frame(topbar, bg=SURFACE)
        right_bar.pack(side="right", padx=20)
        self._time_lbl = tk.Label(right_bar, bg=SURFACE, fg=MUTED,
                                  font=("Segoe UI", 8))
        self._time_lbl.pack(anchor="e")
        self._sys_status = tk.Label(right_bar, text="\u25cf  System Ready",
                                    bg=SURFACE, fg=EMERALD, font=("Segoe UI", 8))
        self._sys_status.pack(anchor="e")
        self._update_clock()

        # Page container
        self._page_container = tk.Frame(main_area, bg=BG)
        self._page_container.pack(fill="both", expand=True)

        self._main_pages = {}
        upload_page = self._build_upload_page(self._page_container)
        verify_page  = self._build_verify_page(self._page_container)

        for page in (upload_page, verify_page):
            page.place(relx=0, rely=0, relwidth=1, relheight=1)

        self._main_pages["upload"] = upload_page
        self._main_pages["verify"] = verify_page

    def _update_clock(self):
        now = datetime.now().strftime("%d %b %Y    %H:%M:%S")
        try:
            self._time_lbl.config(text=now)
            self.after(1000, self._update_clock)
        except:
            pass

    # ─── HELPERS ─────────────────────────────────────────────────────────────
    def _section(self, parent, title, subtitle=""):
        f = tk.Frame(parent, bg=BG)
        f.pack(fill="x", pady=(16, 4))
        row = tk.Frame(f, bg=BG)
        row.pack(fill="x")
        tk.Frame(row, bg=ACCENT, width=3, height=20).pack(side="left", fill="y")
        txt_f = tk.Frame(row, bg=BG)
        txt_f.pack(side="left", padx=10)
        tk.Label(txt_f, text=title, bg=BG, fg=TXT,
                 font=("Segoe UI Semibold", 10)).pack(anchor="w")
        if subtitle:
            tk.Label(txt_f, text=subtitle, bg=BG, fg=MUTED,
                     font=("Segoe UI", 7)).pack(anchor="w")

    def _card(self, parent):
        card = tk.Frame(parent, bg=CARD, padx=20, pady=16,
                        highlightbackground=BORDER, highlightthickness=1)
        card.pack(fill="x", pady=(0, 4))
        return card

    def _mini_entry(self, parent, icon=""):
        """Compact entry with gold focus border."""
        outer = tk.Frame(parent, bg=BORDER, padx=1, pady=1)
        outer.pack(fill="x")
        inner_f = tk.Frame(outer, bg=CARD2)
        inner_f.pack(fill="x")
        if icon:
            tk.Label(inner_f, text=icon, bg=CARD2,
                     font=("Segoe UI", 10)).pack(side="left", padx=(8, 2), pady=7)

        class _Proxy:
            def __init__(self_, entry, outer_frame):
                self_.entry = entry
                self_._outer = outer_frame
            def get(self_):             return self_.entry.get()
            def delete(self_, a, b):   self_.entry.delete(a, b)
            def insert(self_, p, v):   self_.entry.insert(p, v)
            def bind(self_, seq, func, add=None):
                self_.entry.bind(seq, func, add)

        e = tk.Entry(inner_f, font=("Segoe UI", 10), bg=CARD2, fg=TXT,
                     insertbackground=ACCENT, relief="flat",
                     highlightthickness=0, bd=0)
        e.pack(side="left", fill="x", expand=True, padx=(4, 12), pady=8)
        e.bind("<FocusIn>",  lambda ev: outer.config(bg=ACCENT))
        e.bind("<FocusOut>", lambda ev: outer.config(bg=BORDER))

        return _Proxy(e, outer)

    # ─── UPLOAD PAGE ─────────────────────────────────────────────────────────
    def _build_upload_page(self, parent):
        page = tk.Frame(parent, bg=BG)
        sf   = ScrollFrame(page, bgcolor=BG)
        sf.pack(fill="both", expand=True)
        inner = sf.scroll_frame
        inner.config(padx=24, pady=20)

        # Document section
        self._section(inner, "\U0001f4c4  DOCUMENT INFORMATION",
                      "Select the diploma file to secure and register")
        card1 = self._card(inner)

        file_row = tk.Frame(card1, bg=CARD)
        file_row.pack(fill="x", pady=(0, 0))
        tk.Label(file_row, text="File Dokumen / Ijazah", bg=CARD, fg=TXT2,
                 font=("Segoe UI", 8, "bold")).pack(anchor="w", padx=2, pady=(0, 3))
        file_input = tk.Frame(file_row, bg=CARD)
        file_input.pack(fill="x")

        f_outer = tk.Frame(file_input, bg=BORDER, padx=1, pady=1)
        f_outer.pack(side="left", fill="x", expand=True, padx=(0, 8))
        f_inner = tk.Frame(f_outer, bg=CARD2)
        f_inner.pack(fill="x")
        self.k_file = tk.Entry(f_inner, font=("Segoe UI", 10), bg=CARD2, fg=TXT,
                               insertbackground=ACCENT, relief="flat",
                               highlightthickness=0, bd=0)
        self.k_file.pack(fill="x", padx=12, pady=9)
        self.k_file.bind("<FocusIn>",  lambda e: f_outer.config(bg=ACCENT))
        self.k_file.bind("<FocusOut>", lambda e: f_outer.config(bg=BORDER))

        tk.Button(file_input, text="\u29c6  Browse", command=self._pick_kampus,
                  bg=CARD2, fg=ACCENT, relief="flat",
                  font=("Segoe UI Semibold", 9), padx=18, pady=9,
                  cursor="hand2", activebackground=ACCENT, activeforeground=BG,
                  highlightbackground=BORDER, highlightthickness=1).pack(side="right")

        # Student info section
        self._section(inner, "\U0001f393  STUDENT INFORMATION",
                      "Enter details or auto-fetch from PDDIKTI national registry")
        card2 = self._card(inner)

        # NIM
        nim_wrap = tk.Frame(card2, bg=CARD)
        nim_wrap.pack(fill="x", pady=(0, 10))
        tk.Label(nim_wrap, text="NIM (Nomor Induk Mahasiswa)", bg=CARD, fg=TXT2,
                 font=("Segoe UI", 8, "bold")).pack(anchor="w", padx=2, pady=(0, 3))
        nim_row = tk.Frame(nim_wrap, bg=CARD)
        nim_row.pack(fill="x")
        nim_o = tk.Frame(nim_row, bg=BORDER, padx=1, pady=1)
        nim_o.pack(side="left", fill="x", expand=True, padx=(0, 8))
        nim_i = tk.Frame(nim_o, bg=CARD2)
        nim_i.pack(fill="x")
        tk.Label(nim_i, text="\U0001faba", bg=CARD2, font=("Segoe UI", 11)).pack(
            side="left", padx=(10, 4), pady=8)
        self.k_id = tk.Entry(nim_i, font=("Segoe UI", 10), bg=CARD2, fg=TXT,
                             insertbackground=ACCENT, relief="flat",
                             highlightthickness=0, bd=0)
        self.k_id.pack(side="left", fill="x", expand=True, padx=(4, 12), pady=9)
        self.k_id.bind("<FocusIn>",  lambda e: nim_o.config(bg=ACCENT))
        self.k_id.bind("<FocusOut>", lambda e: nim_o.config(bg=BORDER))
        self.k_id.bind("<KeyRelease>", self._on_nim_key_release)

        tk.Button(nim_row, text="\U0001f50d  Cari PDDIKTI",
                  command=lambda: threading.Thread(
                      target=self._fetch_pddikti_data,
                      args=(self.k_id.get().strip(),), daemon=True).start(),
                  bg=SAPPHIRE, fg=WHITE, relief="flat",
                  font=("Segoe UI Semibold", 9), padx=14, pady=9,
                  cursor="hand2", activebackground="#2563eb").pack(side="right")

        self._pddikti_badge = tk.Label(card2, text="", bg=CARD,
                                       font=("Segoe UI", 7))
        self._pddikti_badge.pack(anchor="w", padx=2)

        # Two-col row: Nama + Kode Unik
        row2 = tk.Frame(card2, bg=CARD)
        row2.pack(fill="x", pady=(0, 8))
        name_col = tk.Frame(row2, bg=CARD)
        name_col.pack(side="left", fill="x", expand=True, padx=(0, 10))
        tk.Label(name_col, text="Nama Mahasiswa", bg=CARD, fg=TXT2,
                 font=("Segoe UI", 8, "bold")).pack(anchor="w", padx=2, pady=(0, 3))
        self.k_name = self._mini_entry(name_col, "\U0001f464")

        code_col = tk.Frame(row2, bg=CARD)
        code_col.pack(side="left", fill="x", expand=True)
        tk.Label(code_col, text="Kode Unik", bg=CARD, fg=TXT2,
                 font=("Segoe UI", 8, "bold")).pack(anchor="w", padx=2, pady=(0, 3))
        self.k_code = self._mini_entry(code_col, "\U0001f511")

        # Campus
        tk.Label(card2, text="Nama Kampus", bg=CARD, fg=TXT2,
                 font=("Segoe UI", 8, "bold")).pack(anchor="w", padx=2, pady=(4, 3))
        self.k_campus = self._mini_entry(card2, "\U0001f3db")

        # Row: NIK + Phone
        row3 = tk.Frame(card2, bg=CARD)
        row3.pack(fill="x", pady=(8, 0))
        nik_col = tk.Frame(row3, bg=CARD)
        nik_col.pack(side="left", fill="x", expand=True, padx=(0, 10))
        tk.Label(nik_col, text="NIK", bg=CARD, fg=TXT2,
                 font=("Segoe UI", 8, "bold")).pack(anchor="w", padx=2, pady=(0, 3))
        self.k_nik = self._mini_entry(nik_col, "\U0001faba")

        phone_col = tk.Frame(row3, bg=CARD)
        phone_col.pack(side="left", fill="x", expand=True)
        tk.Label(phone_col, text="No. Telepon", bg=CARD, fg=TXT2,
                 font=("Segoe UI", 8, "bold")).pack(anchor="w", padx=2, pady=(0, 3))
        self.k_phone = self._mini_entry(phone_col, "\U0001f4de")

        # Address
        tk.Label(card2, text="Alamat", bg=CARD, fg=TXT2,
                 font=("Segoe UI", 8, "bold")).pack(anchor="w", padx=2, pady=(8, 3))
        self.k_address = self._mini_entry(card2, "\U0001f4cd")

        # Photo section
        self._section(inner, "\U0001f4f7  FOTO MAHASISWA",
                      "Optional — embedded in credential record")
        card3 = self._card(inner)
        photo_row = tk.Frame(card3, bg=CARD)
        photo_row.pack(fill="x")
        ph_o = tk.Frame(photo_row, bg=BORDER, padx=1, pady=1)
        ph_o.pack(side="left", fill="x", expand=True, padx=(0, 8))
        ph_i = tk.Frame(ph_o, bg=CARD2)
        ph_i.pack(fill="x")
        tk.Label(ph_i, text="\U0001f5bc", bg=CARD2, font=("Segoe UI", 11)).pack(
            side="left", padx=(10, 4), pady=8)
        self.k_photo = tk.Entry(ph_i, font=("Segoe UI", 10), bg=CARD2, fg=TXT,
                                insertbackground=ACCENT, relief="flat",
                                highlightthickness=0, bd=0)
        self.k_photo.pack(side="left", fill="x", expand=True, padx=(4, 12), pady=9)
        self.k_photo.bind("<FocusIn>",  lambda e: ph_o.config(bg=ACCENT))
        self.k_photo.bind("<FocusOut>", lambda e: ph_o.config(bg=BORDER))
        tk.Button(photo_row, text="Browse", command=self._pick_photo,
                  bg=CARD2, fg=ACCENT, relief="flat",
                  font=("Segoe UI Semibold", 9), padx=18, pady=9,
                  cursor="hand2", activebackground=ACCENT, activeforeground=BG,
                  highlightbackground=BORDER, highlightthickness=1).pack(side="right")

        # Action buttons
        action_frame = tk.Frame(inner, bg=BG, pady=10)
        action_frame.pack(fill="x")

        self.k_clear_btn = tk.Button(action_frame, text="\u21ba  Clear Form",
                                     command=self._clear_kampus_form,
                                     bg=CARD2, fg=AMBER, relief="flat",
                                     font=("Segoe UI Semibold", 10),
                                     padx=22, pady=10, cursor="hand2",
                                     activebackground=AMBER, activeforeground=BG)

        tk.Button(action_frame, text="\u2714  Register & Secure",
                  command=self._do_register,
                  bg=EMERALD, fg="#08090f", relief="flat",
                  font=("Segoe UI Semibold", 11),
                  padx=28, pady=11, cursor="hand2",
                  activebackground="#0ea875").pack(side="right")

        # Output log
        self._section(inner, "\u29c6  OUTPUT LOG", "Registration process output")
        log_card = tk.Frame(inner, bg=CARD, highlightbackground=BORDER,
                            highlightthickness=1)
        log_card.pack(fill="x", pady=(0, 20))
        log_hdr = tk.Frame(log_card, bg=CARD2, pady=6)
        log_hdr.pack(fill="x")
        tk.Label(log_hdr, text="  \u25b6 CONSOLE OUTPUT", bg=CARD2, fg=MUTED,
                 font=("Consolas", 7, "bold")).pack(side="left")
        self.k_out = tk.Text(log_card, height=5, bg="#07090f", fg="#7aa2c0",
                             relief="flat", font=("Consolas", 8), wrap="word",
                             padx=14, pady=8, bd=0, highlightthickness=0,
                             insertbackground=ACCENT)
        self.k_out.pack(fill="both", expand=True, padx=1, pady=(0, 1))
        self.k_out.insert("end", "# System ready. Fill in the form and press Register.\n")

        return page

    # ─── VERIFY PAGE ─────────────────────────────────────────────────────────
    def _build_verify_page(self, parent):
        page = tk.Frame(parent, bg=BG)
        sf   = ScrollFrame(page, bgcolor=BG)
        sf.pack(fill="both", expand=True)
        inner = sf.scroll_frame
        inner.config(padx=24, pady=20)

        # Hero
        hero = tk.Frame(inner, bg="#0b1020",
                        highlightbackground=BORDER, highlightthickness=1)
        hero.pack(fill="x", pady=(0, 16))
        hero_inner = tk.Frame(hero, bg="#0b1020", padx=24, pady=20)
        hero_inner.pack(fill="x")
        tk.Label(hero_inner, text="Verify Academic Credential",
                 bg="#0b1020", fg=WHITE,
                 font=("Segoe UI Semibold", 15)).pack(anchor="w")
        tk.Label(hero_inner,
                 text="Enter student details to authenticate a diploma on the blockchain ledger.",
                 bg="#0b1020", fg=TXT2, font=("Segoe UI", 9)).pack(anchor="w", pady=(4, 0))
        tk.Frame(hero_inner, height=2, bg=ACCENT, width=60).pack(anchor="w", pady=(12, 0))

        self._section(inner, "\u29c6  CREDENTIAL LOOKUP",
                      "Enter exact details as registered")
        card = self._card(inner)

        tk.Label(card, text="Nama Mahasiswa", bg=CARD, fg=TXT2,
                 font=("Segoe UI", 8, "bold")).pack(anchor="w", padx=2, pady=(0, 3))
        self.v_name = self._mini_entry(card, "\U0001f464")

        tk.Label(card, text="NIM", bg=CARD, fg=TXT2,
                 font=("Segoe UI", 8, "bold")).pack(anchor="w", padx=2, pady=(10, 3))
        self.v_nim = self._mini_entry(card, "\U0001faba")

        tk.Label(card, text="Kode Unik", bg=CARD, fg=TXT2,
                 font=("Segoe UI", 8, "bold")).pack(anchor="w", padx=2, pady=(10, 3))
        self.v_code = self._mini_entry(card, "\U0001f511")

        tk.Button(inner, text="\u29c6   VERIFY CREDENTIAL",
                  command=self._do_verify,
                  bg=ACCENT, fg="#08090f", relief="flat",
                  font=("Segoe UI Semibold", 12),
                  pady=14, cursor="hand2",
                  activebackground=ACCENT2,
                  activeforeground=BG).pack(fill="x", pady=(16, 4))

        # Info card
        info_box = tk.Frame(inner, bg=CARD2,
                            highlightbackground=BORDER, highlightthickness=1)
        info_box.pack(fill="x", pady=(8, 20))
        info_inner = tk.Frame(info_box, bg=CARD2, padx=18, pady=14)
        info_inner.pack(fill="x")
        tk.Label(info_inner, text="\u2139  How Verification Works",
                 bg=CARD2, fg=ACCENT, font=("Segoe UI Semibold", 9)).pack(anchor="w")
        steps = [
            "1.  Your input is cross-referenced against the blockchain ledger",
            "2.  SHA-256 hash of the document is matched",
            "3.  AES-256 encrypted label is decrypted with the Kode Unik",
            "4.  Integrity of the entire chain is validated before result",
        ]
        for step in steps:
            tk.Label(info_inner, text=step, bg=CARD2, fg=TXT2,
                     font=("Segoe UI", 8), anchor="w").pack(fill="x", pady=1)

        self.v_verified_file = None
        self.v_verified_code = None

        return page

    # ─── STATUS BAR ───────────────────────────────────────────────────────────
    def _build_statusbar(self):
        bar = tk.Frame(self, bg="#06080f", height=28)
        bar.pack(fill="x", side="bottom")
        bar.pack_propagate(False)
        tk.Frame(bar, height=1, bg=BORDER).pack(fill="x", side="top")
        inner = tk.Frame(bar, bg="#06080f")
        inner.pack(fill="x", padx=16, pady=4)
        self.status = tk.Label(inner, text="\u25c9  System ready \u2014 awaiting input",
                               bg="#06080f", fg=EMERALD,
                               anchor="w", font=("Segoe UI", 8))
        self.status.pack(side="left")
        tk.Label(inner, text="\U0001f512  AES-256  \u00b7  SHA-256  \u00b7  Blockchain",
                 bg="#06080f", fg=MUTED, font=("Segoe UI", 7)).pack(side="right")

    # ─── LOGIC (unchanged from original) ─────────────────────────────────────

    def _log(self, widget, text, clear=True):
        if clear:
            widget.delete("1.0", "end")
        widget.insert("end", text)

    def _set_status(self, text, color=None):
        color = color or TXT2
        try:
            self.status.config(text=f"\u25c9  {text}", fg=color)
            self.update_idletasks()
        except:
            pass

    def _reset_layer_indicators(self):
        pass

    def _set_layer(self, index, color, text):
        pass

    def _pick_kampus(self):
        p = filedialog.askopenfilename(filetypes=[
            ("Semua File Dukungan", "*.pdf;*.docx;*.doc;*.txt;*.png;*.jpg;*.jpeg;*.gif;*.bmp"),
            ("PDF Files", "*.pdf"),
            ("Word Documents", "*.docx;*.doc"),
            ("Text Files", "*.txt"),
            ("Images", "*.png;*.jpg;*.jpeg;*.gif;*.bmp"),
            ("All Files", "*.*")
        ])
        if p:
            self.k_file.delete(0, "end")
            self.k_file.insert(0, p)

    def _on_nim_key_release(self, event):
        if event.keysym in ("Tab", "Shift_L", "Shift_R", "Control_L", "Control_R",
                            "Alt_L", "Alt_R", "Caps_Lock", "Return"):
            return
        nim = self.k_id.get().strip()
        if len(nim) >= 9:
            if hasattr(self, "_nim_search_timer"):
                self.after_cancel(self._nim_search_timer)
            self._nim_search_timer = self.after(800, lambda: threading.Thread(
                target=self._fetch_pddikti_data, args=(nim,), daemon=True).start())

    def _fetch_pddikti_data(self, nim):
        try:
            self._set_status(f"Mencari NIM {nim} di PDDIKTI...", AMBER)
            from pddiktipy import api
            with api() as client:
                res = client.search_mahasiswa(nim)
                if res and isinstance(res, list):
                    mhs_entry = None
                    for item in res:
                        if item.get("nim") == nim:
                            mhs_entry = item
                            break
                    if not mhs_entry:
                        mhs_entry = res[0]

                    mhs_id = mhs_entry.get("id")
                    detail = client.get_detail_mhs(mhs_id)
                    if detail:
                        nama        = detail.get("nama",        mhs_entry.get("nama",        ""))
                        kampus      = detail.get("nama_pt",     mhs_entry.get("nama_pt",     ""))
                        prodi       = detail.get("nama_prodi",  mhs_entry.get("nama_prodi",  ""))
                        jenjang     = detail.get("jenjang",     "")
                        tahun_masuk = detail.get("tahun_masuk", "")
                        status      = detail.get("status_saat_ini", "")
                        self.after(0, lambda: self._fill_pddikti_fields(
                            nama, kampus, prodi, jenjang, tahun_masuk, status, nim))
                    else:
                        nama   = mhs_entry.get("nama",      "")
                        kampus = mhs_entry.get("nama_pt",   "")
                        prodi  = mhs_entry.get("nama_prodi","")
                        self.after(0, lambda: self._fill_pddikti_fields(
                            nama, kampus, prodi, "", "", "", nim))
                else:
                    self.after(0, lambda: self._set_status(
                        f"NIM {nim} tidak ditemukan di PDDIKTI.", CRIMSON))
        except Exception as e:
            self.after(0, lambda: self._set_status(
                f"Gagal memuat data PDDIKTI: {str(e)}", CRIMSON))

    def _fill_pddikti_fields(self, nama, kampus, prodi, jenjang, tahun_masuk, status, nim):
        self.k_name.delete(0, "end")
        self.k_name.insert(0, nama)
        self.k_campus.delete(0, "end")
        self.k_campus.insert(0, kampus)

        self._pddikti_prodi       = prodi
        self._pddikti_jenjang     = jenjang
        self._pddikti_tahun_masuk = tahun_masuk

        kampus_clean = kampus.replace("Sekolah Tinggi Teknologi Terpadu", "STT")\
                             .replace("Universitas", "UNIV")\
                             .replace("Institut Teknologi", "ITB")
        words = kampus_clean.split()
        if len(words) >= 2:
            singkatan = "".join([w[0].upper() for w in words if w[0].isalpha()])
        else:
            singkatan = words[0][:5].upper() if words else "KAMPUS"

        first_name  = nama.split()[0].upper() if nama else "STUDENT"
        unique_code = f"{singkatan}-{nim}-{first_name}"
        self.k_code.delete(0, "end")
        self.k_code.insert(0, unique_code)

        log_msg = f"[PDDIKTI] {nama} | {kampus} | {prodi}"
        if jenjang:     log_msg += f" | {jenjang}"
        if tahun_masuk: log_msg += f" | Angkatan {tahun_masuk}"
        self.k_out.insert("end", f"{log_msg}\n")
        self.k_out.see("end")

        try:
            self._pddikti_badge.config(
                text=f"\u2713 PDDIKTI: {nama} \u2014 {kampus}", fg=EMERALD)
        except:
            pass

        self._set_status(f"PDDIKTI: {nama} \u00b7 {kampus} \u00b7 {prodi}", EMERALD)

    def _pick_photo(self):
        p = filedialog.askopenfilename(
            filetypes=[("Images", "*.png;*.jpg;*.jpeg;*.gif;*.bmp")])
        if p:
            self.k_photo.delete(0, "end")
            self.k_photo.insert(0, p)

    def _clear_kampus_form(self):
        for w in (self.k_code, self.k_name, self.k_nik, self.k_phone,
                  self.k_campus, self.k_address):
            w.delete(0, "end")
        self.k_file.delete(0, "end")
        self.k_id.delete(0, "end")
        self.k_photo.delete(0, "end")
        self._log(self.k_out, "# Form cleared.\n")
        self.k_clear_btn.pack_forget()
        self._set_status("Form cleared.", TXT2)
        try:
            self._pddikti_badge.config(text="", fg=MUTED)
        except:
            pass

    def _do_register(self):
        src    = self.k_file.get().strip()
        code   = self.k_code.get().strip()
        name   = self.k_name.get().strip()
        sid    = self.k_id.get().strip()
        campus = self.k_campus.get().strip()
        nik    = self.k_nik.get().strip()
        phone  = self.k_phone.get().strip()
        addr   = self.k_address.get().strip()
        photo  = self.k_photo.get().strip()

        if not (src and code and name and sid and campus and nik and phone and addr):
            messagebox.showwarning("Incomplete", "All fields required (photo optional).")
            return
        if not os.path.exists(src):
            messagebox.showerror("Error", f"File not found:\n{src}")
            return
        if photo and not os.path.exists(photo):
            messagebox.showerror("Error", f"Photo not found:\n{photo}")
            return

        self._set_status("Geocoding address...", AMBER)
        threading.Thread(
            target=self._register_worker,
            args=(src, code, name, sid, campus, nik, phone, addr, photo),
            daemon=True).start()

    def _register_worker(self, src, code, name, sid, campus, nik, phone, addr, photo):
        temp_secured = None
        try:
            coord        = geocode_address(addr)
            coord_str    = f"{coord['lat']}, {coord['lon']}" if coord else ""
            os.makedirs(SECURED_FOLDER, exist_ok=True)
            temp_secured = os.path.join(SECURED_FOLDER, f"temp_{sid}_SECURED.pdf")
            sig          = pdf_secure.stamp_and_secure(src, temp_secured, code, name, sid)

            photo_b64 = ""
            if photo:
                try:
                    with Image.open(photo) as img:
                        if img.mode != "RGB":
                            img = img.convert("RGB")
                        img.thumbnail((150, 150))
                        buf = io.BytesIO()
                        img.save(buf, format="JPEG", quality=60)
                        photo_b64 = base64.b64encode(buf.getvalue()).decode("utf-8")
                except Exception as e:
                    print("Photo error:", e)

            details = {
                "nik": nik, "phone": phone, "address": addr, "campus": campus,
                "prodi":       getattr(self, "_pddikti_prodi",       ""),
                "jenjang":     getattr(self, "_pddikti_jenjang",     ""),
                "tahun_masuk": getattr(self, "_pddikti_tahun_masuk", ""),
                "photo": photo_b64, "coord": coord_str
            }
            details_json = json.dumps(details)
            details_b64  = base64.b64encode(details_json.encode("utf-8")).decode("utf-8")
            res          = scdv_core.register(temp_secured, code, name, sid, details_b64)

            secured_path = temp_secured
            if res.get("STATUS") == "OK":
                file_hash    = res.get("HASH", "")
                secured_fn   = f"{file_hash}.pdf"
                secured_path = os.path.join(SECURED_FOLDER, secured_fn)
                shutil.copy2(temp_secured, secured_path)

                manifest = {}
                if os.path.exists(MANIFEST_FILE):
                    with open(MANIFEST_FILE, "r") as f:
                        manifest = json.load(f)
                manifest[sid] = {
                    "name": name, "code": code, "hash": file_hash,
                    "signature": sig, "timestamp": datetime.now().isoformat(),
                    "secured_file": secured_fn, "details": details
                }
                with open(MANIFEST_FILE, "w") as f:
                    json.dump(manifest, f, indent=2)

            self.after(0, self._register_done, res, secured_path, sig, code)
        except Exception as e:
            self.after(0, lambda: messagebox.showerror("Failed", str(e)))
            self.after(0, lambda: self._set_status("Registration failed.", CRIMSON))
        finally:
            if temp_secured and os.path.exists(temp_secured):
                try: os.remove(temp_secured)
                except: pass

    def _register_done(self, res, out, sig, code):
        if res.get("STATUS") == "OK":
            fh = res.get("HASH", "")
            self._log(self.k_out,
                      f"\u2714  Registration successful\n"
                      f"   Hash  : {fh[:48]}...\n"
                      f"   Sig   : {sig[:32]}...\n"
                      f"   Code  : {code}\n"
                      f"   Saved : secured/\n")
            self._set_status("Done \u2014 diploma secured and registered.", EMERALD)
            self.k_clear_btn.pack(side="left", padx=(0, 8))
            if messagebox.askyesno("Success", "Diploma secured.\nOpen secured folder?"):
                os.startfile(SECURED_FOLDER)
        else:
            msg = res.get("MESSAGE", "Unknown error")
            self._log(self.k_out, f"\u2717  {msg}\n")
            self._set_status("Failed.", CRIMSON)

    def _do_verify(self):
        name = self.v_name.get().strip()
        nim  = self.v_nim.get().strip()
        code = self.v_code.get().strip()

        if not (name and nim and code):
            messagebox.showwarning("Incomplete",
                                   "All three fields required:\nNama + NIM + Kode Unik")
            return

        self._reset_layer_indicators()
        scan = ScanPopup(self)
        scan.log_left(f"Target: {name} | NIM: {nim}")
        scan.log_right(f"Code: {code[:4]}...{code[-4:]}")
        threading.Thread(target=self._verify_with_scan,
                         args=(scan, name, nim, code), daemon=True).start()

    def _lookup_details_from_manifest(self, sid):
        try:
            if os.path.exists(MANIFEST_FILE):
                with open(MANIFEST_FILE, "r") as f:
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
                scan.log_left(f"Secured files: {len(secured_files)}")
                scan.log_right("Scanning secured folder...")
            else:
                scan.log_left("No secured files found")
                scan.log_right("Querying blockchain directly...")

            find_res = scdv_core.find_by_label(code)

            if find_res.get("STATUS") == "FOUND":
                block_name = find_res.get("NAME", "")
                block_id   = find_res.get("ID", "")
                scan.log_left(f"Block: {block_name} | {block_id}")
                name_ok = block_name.lower() == name.lower()
                nim_ok  = block_id == nim
                scan.log_right("OK" if name_ok else "MISMATCH")
                scan.log_left("OK"  if nim_ok  else "MISMATCH")

                if name_ok and nim_ok:
                    file_hash    = find_res.get("HASH", "")
                    secured_path = os.path.join(SECURED_FOLDER, f"{file_hash}.pdf")
                    if os.path.exists(secured_path):
                        opens       = pdf_secure.can_open(secured_path, code)
                        details_b64 = find_res.get("DETAILS", "")
                        if not details_b64:
                            details_b64 = self._lookup_details_from_manifest(block_id)
                        scan.log_left("File matched")
                        scan.log_right(f"PDF: {'UNLOCKED' if opens else 'LOCKED'}")
                        scan.set_success()
                        time.sleep(0.2)
                        data_dict = self._build_data_dict(find_res, details_b64)
                        self.after(0, lambda: self._show_result_popups(
                            data_dict, secured_path, code, opens))
                        self.after(0, lambda: self._update_main_view(
                            find_res, details_b64, opens, secured_path, code))
                        self.after(150, scan.close)
                    else:
                        scan.log_left("Hash mismatch")
                        scan.set_fail()
                        self.after(0, lambda: messagebox.showerror(
                            "Error", "Secured file not found in storage."))
                        self.after(150, scan.close)
                else:
                    scan.log_left("Name/NIM mismatch \u2014 fallback")
                    self._scan_fallback(scan, name, nim, code)
                return

            scan.log_left("Code not in blockchain")
            scan.log_right("Fallback: name+NIM")
            self._scan_fallback(scan, name, nim, code)

        except Exception as e:
            scan.log_left(f"ERROR: {str(e)[:30]}")
            scan.set_fail()
            self.after(0, lambda: messagebox.showerror("Error", str(e)))
            self.after(150, scan.close)

    def _scan_fallback(self, scan, name, nim, code):
        try:
            scan.log_left("Fallback: name+NIM query")
            scan.log_right("Querying blockchain...")
            find_res = scdv_core.find_by_student(name, nim)

            if find_res.get("STATUS") == "FOUND":
                file_hash    = find_res.get("HASH", "")
                secured_path = os.path.join(SECURED_FOLDER, f"{file_hash}.pdf")
                scan.log_left(f"Found: {find_res.get('NAME', '')}")

                if os.path.exists(secured_path):
                    opens = pdf_secure.can_open(secured_path, code)
                    if not opens:
                        scan.log_left("Wrong Code")
                        scan.log_right("Access Denied")
                        scan.set_fail()
                        self.after(0, lambda: messagebox.showerror(
                            "Access Denied",
                            "Kode Unik salah untuk mahasiswa ini.\n"
                            "Akses dokumen dan data detail ditolak."))
                        self.after(150, scan.close)
                        return

                    details_b64 = find_res.get("DETAILS", "")
                    if not details_b64:
                        details_b64 = self._lookup_details_from_manifest(nim)

                    scan.log_left("Record verified")
                    scan.log_right(f"PDF: {'UNLOCKED' if opens else 'DIFFERENT CODE'}")
                    scan.set_success()
                    time.sleep(0.2)
                    data_dict = self._build_data_dict(find_res, details_b64)
                    self.after(0, lambda: self._show_result_popups(
                        data_dict, secured_path, code, opens))
                    self.after(0, lambda: self._update_main_view(
                        find_res, details_b64, opens, secured_path, code))
                    self.after(150, scan.close)
                else:
                    scan.log_left("File missing")
                    scan.set_fail()
                    self.after(0, lambda: messagebox.showerror(
                        "Error", "Secured file not found."))
                    self.after(150, scan.close)
            else:
                scan.log_left("NOT FOUND")
                scan.log_right("No matching record")
                scan.set_fail()
                self.after(0, lambda: messagebox.showerror(
                    "Not Found", "No matching record in blockchain."))
                self.after(0, lambda: self._set_status(
                    "NOT FOUND \u2014 no match.", CRIMSON))
                self.after(150, scan.close)

        except Exception as e:
            scan.log_left(f"ERROR: {str(e)[:30]}")
            scan.set_fail()
            self.after(0, lambda: messagebox.showerror("Error", str(e)))
            self.after(150, scan.close)

    def _build_data_dict(self, find_res, details_b64):
        d = {}
        d["Nama Lengkap"] = find_res.get("NAME", "-")
        d["NIM"]          = find_res.get("ID",   "-")
        d["Terdaftar"]    = find_res.get("TIME", "-")

        details_dict = {}
        if details_b64:
            if isinstance(details_b64, dict):
                details_dict = details_b64
            elif isinstance(details_b64, str):
                try:
                    details_json = base64.b64decode(details_b64).decode("utf-8")
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

        d["NIK"]          = details_dict.get("nik",         "-")
        d["No. Telepon"]  = details_dict.get("phone",       "-")
        d["Alamat"]       = details_dict.get("address",     "-")
        d["Nama Kampus"]  = details_dict.get("campus",      "-")
        d["Program Studi"]= details_dict.get("prodi",       "-")
        d["Jenjang"]      = details_dict.get("jenjang",     "-")
        d["Angkatan"]     = details_dict.get("tahun_masuk", "-")
        coord             = details_dict.get("coord", "")
        d["Koordinat"]    = coord if coord else "-"
        d["Foto"]         = "Available" if details_dict.get("photo") else "None"
        d["photo_b64"]    = details_dict.get("photo", "")
        return d

    def _show_result_popups(self, data_dict, pdf_path, code, opens):
        ResultPopup(self, "Verification Result",
                    data_dict=data_dict, pdf_path=pdf_path,
                    code=code, opens=opens)

    def _update_main_view(self, find_res, details_b64, opens, secured_path, code):
        pass

    def _verify_error(self, msg):
        self._set_status(f"Error: {msg}", CRIMSON)
        messagebox.showerror("Error", msg)

    def _reset_verify_view(self, status_text, bg_color="#1a0a0a", fg_color=CRIMSON):
        pass

    def _set_verified_view(self):
        pass

    def _open_verified_pdf(self):
        pass

    def _do_validate(self):
        try:
            res = scdv_core.validate()
            if res.get("STATUS") == "VALID":
                messagebox.showinfo("Chain Integrity",
                                    "\u2713  Blockchain VALID\n"
                                    "No tampering detected across all blocks.")
                self._set_status("Blockchain integrity: VALID", EMERALD)
            else:
                messagebox.showwarning("Chain Integrity",
                                       "\u26a0  Blockchain INVALID\n"
                                       "Tampering detected!")
                self._set_status("Blockchain integrity: INVALID", CRIMSON)
        except Exception as e:
            messagebox.showerror("Error", str(e))


if __name__ == "__main__":
    App().mainloop()
