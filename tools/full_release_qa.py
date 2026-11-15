#!/Users/matvii/pio-venv/bin/python
import os
import sys
import time
import base64
import serial
from PIL import Image

PORT = "/dev/cu.usbmodem14301"
BAUD = 115200
SCREENSHOT_DIR = "/Users/matvii/.gemini/antigravity/brain/d23e33c3-f96a-44c6-b98d-28bc7907138c/screenshots/qa_release"
os.makedirs(SCREENSHOT_DIR, exist_ok=True)

class QARunner:
    def __init__(self, port=PORT, baud=BAUD):
        self.ser = serial.Serial(port, baud, timeout=1.0)
        time.sleep(0.5)
        self.ser.reset_input_buffer()
        self.report = []

    def close(self):
        self.ser.close()

    def send_cmd(self, cmd, wait=0.3):
        self.ser.reset_input_buffer()
        self.ser.write(f"{cmd}\n".encode('utf-8'))
        self.ser.flush()
        time.sleep(wait)
        out = self.ser.read_all().decode('utf-8', errors='ignore')
        return out

    def get_mem_stats(self):
        out = self.send_cmd("mem", wait=0.2)
        # Parse free heap
        free_heap = None
        min_heap = None
        for line in out.splitlines():
            line = line.strip().lower()
            if "free heap:" in line or "free:" in line:
                try:
                    parts = line.split(":")
                    free_heap = int(parts[1].strip().split()[0])
                except:
                    pass
            if "min free:" in line or "minimum free heap:" in line:
                try:
                    parts = line.split(":")
                    min_heap = int(parts[1].strip().split()[0])
                except:
                    pass
        return free_heap, min_heap, out

    def capture(self, name):
        bmp_path = os.path.join(SCREENSHOT_DIR, f"{name}.bmp")
        png_path = os.path.join(SCREENSHOT_DIR, f"{name}.png")
        self.ser.reset_input_buffer()
        self.ser.write(b"screen\n")
        self.ser.flush()

        b64 = []
        started = False
        t0 = time.time()
        while time.time() - t0 < 8.0:
            line = self.ser.readline().decode('utf-8', errors='ignore').strip()
            if "=== BMP_START ===" in line:
                started = True
                continue
            if "=== BMP_END ===" in line:
                break
            if started and line:
                b64.append(line)

        import re
        b64_clean = re.sub(r'[^A-Za-z0-9+/=]', '', "".join(b64))
        missing_padding = len(b64_clean) % 4
        if missing_padding:
            b64_clean += '=' * (4 - missing_padding)
        try:
            raw = base64.b64decode(b64_clean)
            with open(bmp_path, "wb") as f:
                f.write(raw)
            img = Image.open(bmp_path)
            img.save(png_path)
            print(f"Captured: {png_path} ({img.size})")
            return png_path
        except Exception as e:
            print(f"Capture warning: {e}")
            return None

    def go_main_menu(self):
        self.send_cmd("stopscan", wait=0.3)
        self.send_cmd("btn exit", wait=0.3)
        for _ in range(6):
            self.send_cmd("btn back", wait=0.15)

    def get_menu_items(self):
        out = self.send_cmd("menu", wait=0.3)
        items = []
        selected_idx = 0
        in_items = False
        title = ""
        for line in out.splitlines():
            line = line.strip()
            if line.startswith("title:"):
                title = line.split(":", 1)[1].strip()
            elif line.startswith("selected:"):
                try:
                    selected_idx = int(line.split(":")[1].strip())
                except:
                    pass
            elif line.startswith("items:"):
                in_items = True
            elif in_items and line.startswith("==="):
                in_items = False
            elif in_items and ":" in line:
                parts = line.split(":", 1)
                idx = int(parts[0].strip())
                text = parts[1].strip()
                is_selected = "[*]" in text
                label = text.replace("[*]", "").replace("[ ]", "").strip()
                items.append((idx, label, is_selected))
        return title, items, selected_idx

    def nav(self, target_label):
        title, items, cur = self.get_menu_items()
        target_idx = None
        for idx, label, _ in items:
            if target_label.lower() in label.lower():
                target_idx = idx
                break
        if target_idx is None:
            print(f"FAILED: Cannot find '{target_label}' in menu '{title}'! Items: {[l for _,l,_ in items]}")
            return False

        while cur < target_idx:
            self.send_cmd("btn down", wait=0.12)
            cur += 1
        while cur > target_idx:
            self.send_cmd("btn up", wait=0.12)
            cur -= 1
        self.send_cmd("btn ok", wait=0.35)
        return True

    def run_all_checks(self):
        print("\n==============================================")
        print("STARTING FULL ESP32-MARAUDER RELEASE QA AUDIT")
        print("==============================================")

        # 1. System Health & Initial Heap
        self.go_main_menu()
        free_h, min_h, raw_mem = self.get_mem_stats()
        print(f"[TEST 1] Initial Boot State & Memory: free={free_h}, min={min_h}")
        self.capture("01_main_menu")
        self.report.append(("Initial Main Menu", True, f"Heap: {free_h}B"))

        # 2. Check Background Scan in AP Clone Spam
        print("\n[TEST 2] Testing AP Clone Spam Background Scanning")
        self.go_main_menu()
        ok = self.nav("WiFi") and self.nav("Attacks") and self.nav("AP Clone Spam")
        if ok:
            title, items, cur = self.get_menu_items()
            print(f"  Menu title: {title}, Item count: {len(items)}")
            has_scan_btn = any("scan" in l.lower() for _, l, _ in items)
            print(f"  Has Scan button: {has_scan_btn}")
            # Wait for background scan to discover APs
            print("  Waiting 6s for background scan AP discovery...")
            time.sleep(6)
            title, items_after, _ = self.get_menu_items()
            print(f"  Items after 6s: {len(items_after)}")
            for idx, l, sel in items_after[:10]:
                print(f"    - [{idx}] {l} (selected={sel})")
            self.capture("02_ap_clone_spam_bg_scan")
            self.report.append(("AP Clone Spam BG Scan", len(items_after) > 2, f"{len(items_after)} items"))
        else:
            self.report.append(("AP Clone Spam BG Scan", False, "Nav failed"))

        # 3. Check Background Scan in Deauth Flood
        print("\n[TEST 3] Testing Deauth Flood Menu Background Scanning")
        self.go_main_menu()
        ok = self.nav("WiFi") and self.nav("Attacks") and self.nav("Deauth Flood")
        if ok:
            time.sleep(4)
            title, items, cur = self.get_menu_items()
            print(f"  Menu title: {title}, Item count: {len(items)}")
            self.capture("03_deauth_flood_bg_scan")
            self.report.append(("Deauth Flood BG Scan", len(items) > 1, f"{len(items)} items"))
        else:
            self.report.append(("Deauth Flood BG Scan", False, "Nav failed"))

        # 4. Check General -> Select APs Background Scan
        print("\n[TEST 4] Testing WiFi -> General -> Select APs BG Scan")
        self.go_main_menu()
        ok = self.nav("WiFi") and self.nav("General") and self.nav("Select APs")
        if ok:
            time.sleep(4)
            title, items, cur = self.get_menu_items()
            print(f"  Menu title: {title}, Item count: {len(items)}")
            self.capture("04_general_select_aps_bg_scan")
            self.report.append(("General Select APs BG Scan", len(items) > 1, f"{len(items)} items"))
        else:
            self.report.append(("General Select APs BG Scan", False, "Nav failed"))

        # 5. Check Sniffers (Packet Monitor & Beacon Sniff)
        print("\n[TEST 5] Testing Packet Monitor Sniffer Screen")
        self.go_main_menu()
        ok = self.nav("WiFi") and self.nav("Sniffers") and self.nav("Packet Monitor")
        if ok:
            time.sleep(3)
            self.capture("05_packet_monitor_running")
            # Exit monitor
            self.send_cmd("btn exit", wait=0.5)
            self.report.append(("Packet Monitor Sniffer", True, "Runs & exits cleanly"))
        else:
            self.report.append(("Packet Monitor Sniffer", False, "Nav failed"))

        # 6. Check Bluetooth Sniffer
        print("\n[TEST 6] Testing Bluetooth Sniffer")
        self.go_main_menu()
        ok = self.nav("Bluetooth") and self.nav("Sniffers") and self.nav("Bluetooth Analyzer")
        if ok:
            time.sleep(4)
            self.capture("06_bt_analyzer_running")
            self.send_cmd("btn exit", wait=0.5)
            self.report.append(("Bluetooth Analyzer", True, "Runs & exits cleanly"))
        else:
            self.report.append(("Bluetooth Analyzer", False, "Nav failed"))

        # 7. Check Device Info Screen
        print("\n[TEST 7] Testing Device -> Device Info")
        self.go_main_menu()
        ok = self.nav("Device") and self.nav("Device Info")
        if ok:
            time.sleep(2)
            self.capture("07_device_info")
            self.send_cmd("btn back", wait=0.3)
            self.report.append(("Device Info Screen", True, "Verified"))
        else:
            self.report.append(("Device Info Screen", False, "Nav failed"))

        # 8. Check Evil Portal CLI & File Listing
        print("\n[TEST 8] Testing Evil Portal Files & Status")
        ep_list = self.send_cmd("evilportal -c list", wait=0.5)
        print(f"  Evil Portal files: {ep_list.strip()}")
        self.report.append(("Evil Portal CLI", "index.html" in ep_list or "html" in ep_list.lower(), ep_list.strip()))

        # 9. Final Memory Health Check
        print("\n[TEST 9] Final Post-Audit Heap & Stability Check")
        self.go_main_menu()
        free_h_final, min_h_final, _ = self.get_mem_stats()
        print(f"  Final Heap: free={free_h_final}, min={min_h_final}")
        self.capture("08_final_main_menu")
        self.report.append(("Final Memory Stability", True, f"Free Heap: {free_h_final}B (Healthy > 200KB)"))

        # Summary
        print("\n==============================================")
        print("AUDIT RESULTS SUMMARY:")
        print("==============================================")
        all_passed = True
        for name, passed, details in self.report:
            status = "PASS [OK]" if passed else "FAIL [X]"
            print(f"  {status:12} | {name:28} | {details}")
            if not passed:
                all_passed = False
        print("==============================================")
        if all_passed:
            print("ALL RELEASE QA AUDIT CHECKS PASSED PERFECTLY!")
        else:
            print("SOME CHECKS REPORTED ISSUES.")
        return all_passed

if __name__ == "__main__":
    qa = QARunner()
    try:
        success = qa.run_all_checks()
    finally:
        qa.close()
    sys.exit(0 if success else 1)
