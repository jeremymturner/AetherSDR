// Unit test for IcomDiscovery's pure subnet-enumeration helper (#5).
// Freestanding int main() returning 0/1, no framework, links Qt6::Core.
#include "core/IcomDiscovery.h"

#include <QtGlobal>

#include <cstdio>

using AetherSDR::icomSweepHosts;

namespace {

int g_fails = 0;

void check(bool ok, const char* what)
{
    if (!ok) {
        std::fprintf(stderr, "icom_discovery_test: FAIL: %s\n", what);
        ++g_fails;
    }
}

constexpr quint32 ip(quint32 a, quint32 b, quint32 c, quint32 d)
{
    return (a << 24) | (b << 16) | (c << 8) | d;
}

}  // namespace

int main()
{
    constexpr int kCap = 1024;

    // /24: 254 usable hosts, .1 .. .254
    {
        const auto hosts = icomSweepHosts(ip(192, 168, 1, 50), ip(255, 255, 255, 0), kCap);
        check(hosts.size() == 254, "/24 host count");
        check(!hosts.isEmpty() && hosts.first() == ip(192, 168, 1, 1), "/24 first host");
        check(!hosts.isEmpty() && hosts.last() == ip(192, 168, 1, 254), "/24 last host");
    }

    // /25: 126 usable hosts (.1 .. .126 for the lower half)
    {
        const auto hosts = icomSweepHosts(ip(10, 0, 0, 5), ip(255, 255, 255, 128), kCap);
        check(hosts.size() == 126, "/25 host count");
        check(!hosts.isEmpty() && hosts.first() == ip(10, 0, 0, 1), "/25 first host");
        check(!hosts.isEmpty() && hosts.last() == ip(10, 0, 0, 126), "/25 last host");
    }

    // /30: exactly 2 usable hosts
    {
        const auto hosts = icomSweepHosts(ip(172, 16, 0, 1), ip(255, 255, 255, 252), kCap);
        check(hosts.size() == 2, "/30 host count");
    }

    // /31 and /32: no usable host range
    check(icomSweepHosts(ip(192, 168, 0, 0), ip(255, 255, 255, 254), kCap).isEmpty(),
          "/31 empty");
    check(icomSweepHosts(ip(192, 168, 0, 7), ip(255, 255, 255, 255), kCap).isEmpty(),
          "/32 empty");

    // /16: 65534 hosts > cap → skipped (empty), never flood a large subnet.
    check(icomSweepHosts(ip(172, 17, 3, 4), ip(255, 255, 0, 0), kCap).isEmpty(),
          "/16 skipped over cap");

    // /22: 1022 hosts, under the 1024 cap → swept.
    check(icomSweepHosts(ip(192, 168, 4, 9), ip(255, 255, 252, 0), kCap).size() == 1022,
          "/22 within cap");

    // Zero mask → nothing.
    check(icomSweepHosts(ip(192, 168, 1, 1), 0, kCap).isEmpty(), "zero mask empty");

    if (g_fails == 0) {
        std::printf("icom_discovery_test: PASS\n");
    }
    return g_fails == 0 ? 0 : 1;
}
