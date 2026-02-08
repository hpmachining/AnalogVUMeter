# Maintainer: hpmachining <aur at hpminc dot com>
pkgname=analogvumeter
pkgver=1.0.0
pkgrel=1
pkgdesc="A desktop application that visually replicates a classic analog stereo VU meter"
arch=('x86_64')
url="https://github.com/hpmachining/AnalogVUMeter"
license=('MIT')
depends=('qt6-base' 'pulseaudio' 'libzip')
makedepends=('cmake' 'pkgconf')
source=("$pkgname-$pkgver.tar.gz::https://github.com/hpmachining/AnalogVUMeter/archive/refs/tags/v$pkgver.tar.gz")
sha256sums=('SKIP')

build() {
    cd "AnalogVUMeter-$pkgver"
    cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
    cmake --build build
}

package() {
    cd "AnalogVUMeter-$pkgver"
    install -Dm755 "build/analog_vu_meter" "$pkgdir/usr/bin/analog_vu_meter"
    install -Dm644 "LICENSE" "$pkgdir/usr/share/licenses/$pkgname/LICENSE"
    install -Dm644 "README.md" "$pkgdir/usr/share/doc/$pkgname/README.md"
}

# vim:set ts=2 sw=2 et:
