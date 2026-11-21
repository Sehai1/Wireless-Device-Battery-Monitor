// ATK V75X and Wobkey Rainy 75 (Evision) keyboards on the 2.4 GHz receiver (320F:5088) or USB cable (320F:5055).
//   Output report 04 on FF1C:0092, 31 payload bytes: [04][seq lo][seq hi][1A][06/00 ...]
//   Input report 04: [04][seq lo][seq hi][1A][..][00 00][..][percent][state: 0 on battery, 1/2 charging]
#include "drivers.h"

#include <cwctype>
#include <map>

namespace {

constexpr USHORT kVid = 0x320F, kReceiver = 0x5088, kCable = 0x5055;

std::wstring LowerStr(std::wstring s) {
    for (auto& ch : s) ch = (wchar_t)towlower(ch);
    return s;
}

bool ReadBattery(const HidColl& c, int& percent, bool& charging) {
    static USHORT seq = 0;
    seq = (USHORT)(seq % 0xFFFF + 1);
    HidDev d;
    if (!d.Open(c)) return false;
    d.Flush();

    // Try standard Evision battery command (with 0x06 length hint first, then 0x00)
    for (BYTE subHint : { (BYTE)0x06, (BYTE)0x00 }) {
        std::vector<BYTE> req(32, 0);
        req[0] = 0x04;
        req[1] = (BYTE)(seq & 0xFF);
        req[2] = (BYTE)(seq >> 8);
        req[3] = 0x1A;
        req[4] = subHint;
        if (!d.Write(req)) continue;

        ULONGLONG end = GetTickCount64() + 600;
        std::vector<BYTE> r;
        while ((LONGLONG)end - (LONGLONG)GetTickCount64() > 0) {
            int n = d.Read(r, 100);
            if (n < 10 || r[0] != 0x04 || r[3] != 0x1A || r[5] || r[6])
                continue;
            if (r[8] < 1 || r[8] > 100 || r[9] > 2)
                continue;
            percent = r[8];
            charging = r[9] == 1 || r[9] == 2;
            return true;
        }
    }
    return false;
}

}  // namespace

void PollAtkKeyboard(PollContext& ctx) {
    for (USHORT pid : { kCable, kReceiver }) {  // the cable wins when both are connected
        if (ctx.IsClaimed(kVid, pid)) continue;
        bool cable = pid == kCable;
        for (const HidColl* c : ctx.Find(kVid, pid, [](const HidColl& c) { return c.page() == 0xFF1C && c.usage() == 0x0092; })) {
            std::wstring name = HidProduct(*c);
            while (!name.empty() && name.back() == L' ') name.pop_back();
            std::wstring low = LowerStr(name);

            std::wstring displayName = L"Rainy 75";
            if (low.find(L"atk") != std::wstring::npos || low.find(L"v75x") != std::wstring::npos) {
                displayName = L"ATK V75X";
            } else if (!name.empty() && low.find(L"2.4g") == std::wstring::npos && low.find(L"receiver") == std::wstring::npos &&
                       low.find(L"dongle") == std::wstring::npos && low.find(L"usb") == std::wstring::npos) {
                displayName = name;
            }

            DeviceStatus s;
            s.key = L"evision_kb:5088";
            s.name = displayName;
            s.kind = DeviceKind::Keyboard;
            s.via = cable ? L"USB cable" : L"2.4 GHz receiver";
            s.graceSec = 1800;

            int percent = -1;
            bool charging = false;
            if (ReadBattery(*c, percent, charging)) {
                s.percent = percent;
                s.charging = charging;
            } else {
                s.asleep = true;
            }
            ctx.Add(s, kVid, pid);
            if (cable) ctx.Claim(kVid, kReceiver);
            return;
        }
    }
}
