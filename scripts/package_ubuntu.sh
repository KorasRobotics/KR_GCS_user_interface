#!/usr/bin/env bash
# KR_GCS_user_interface 우분투 22.04 배포본 만들기
#
# 결과 (출력 폴더 아래):
#   KR_GCS_user_interface_ubuntu/      실행: kr_gcs_user_interface.sh
#   KR_GCS_user_interface_ubuntu.7z    GitHub Release 첨부 파일 (사용자 매뉴얼의 이름과 같다)
#
# Qt·libmodbus·jsoncpp와, 우분투 데스크톱에 기본으로 없을 수 있는 xcb 보조 라이브러리를 함께 넣어
# 받는 PC에 Qt를 설치하지 않아도 실행되게 한다. glibc·libstdc++·X11·OpenGL·fontconfig 같은
# 시스템 라이브러리는 넣지 않는다(드라이버·배포판과 맞물려 있어 넣으면 오히려 깨진다).
#
# 사용 (우분투 22.04 또는 WSL Ubuntu-22.04):
#   sudo scripts/package_ubuntu.sh --deps    # 빌드에 필요한 apt 패키지 설치 (처음 한 번)
#   scripts/package_ubuntu.sh [출력 폴더]     # 빌드 + 묶기 + 7z (기본 출력: build-ubuntu-release/)
set -euo pipefail

SRC_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
APP=kr_gcs_user_interface
PKG_NAME=KR_GCS_user_interface_ubuntu

if [[ "${1:-}" == "--deps" ]]; then
    apt-get update
    apt-get install -y build-essential cmake pkg-config file patchelf p7zip-full \
        qt6-base-dev qt6-base-dev-tools qt6-qpa-plugins libgl1-mesa-dev \
        libqt6svg6 libmodbus-dev libjsoncpp-dev libboost-dev \
        libxkbcommon-x11-0 libxcb-icccm4 libxcb-image0 libxcb-keysyms1 \
        libxcb-render-util0 libxcb-xinerama0 libxcb-xkb1
    # SVG 아이콘 엔진 플러그인은 배포판에 따라 별도 패키지다 — 없으면 건너뛴다
    apt-get install -y qt6-svg-plugins 2>/dev/null || true
    exit 0
fi

OUT_DIR="$(realpath -m "${1:-$SRC_DIR/build-ubuntu-release}")"
BUILD_DIR="$OUT_DIR/build"
PKG_DIR="$OUT_DIR/$PKG_NAME"

# ---- 0. .ui enum 표기 검사 ------------------------------------------------------------------
# Qt Creator 6.4 이상의 Designer는 Qt::Orientation::Vertical처럼 긴 enum 이름을 쓴다. 우분투 22.04(Qt 6.2)와
# ROS2 Humble(Qt 5.15)의 uic는 이를 읽지 못하고 기본값을 써서 세로 구분선이 가로선이 된다(빌드 오류는 없다).
if grep -nE '[A-Za-z]+::[A-Za-z]+::[A-Za-z]+' "$SRC_DIR"/ui/*.ui; then
    echo "위 .ui 줄의 긴 enum 이름을 짧은 이름(Qt::Vertical, Qt::AlignCenter)으로 바꾼 뒤 다시 실행하세요:"
    echo "  sed -i -E 's/\b([A-Za-z]+)::[A-Za-z]+::([A-Za-z]+)/\1::\2/g' ui/*.ui"
    exit 1
fi

# ---- 1. 빌드 ------------------------------------------------------------------------------
# Qt5 개발 패키지(예: ROS2 Humble)도 깔려 있으면 find_package(QT NAMES Qt6 Qt5)가 Qt5를 잡아, 아래에서 묶는
# Qt6 플러그인과 맞지 않는다. Qt6로 고정한다.
QT6_CMAKE=$(find /usr/lib /usr/local/lib -path '*/cmake/Qt6/Qt6Config.cmake' 2>/dev/null | head -1)
[[ -n "$QT6_CMAKE" ]] || { echo "Qt6 CMake 설정을 찾지 못했습니다 (qt6-base-dev 설치 확인)"; exit 1; }
QT6_CMAKE_DIR="$(dirname "$QT6_CMAKE")"
cmake -S "$SRC_DIR" -B "$BUILD_DIR" -DCMAKE_BUILD_TYPE=Release -DQT_DIR="$QT6_CMAKE_DIR" -DQt6_DIR="$QT6_CMAKE_DIR"
cmake --build "$BUILD_DIR" -j"$(nproc)"

# ---- 2. 실행 파일과 Qt 플러그인 -------------------------------------------------------------
QT_PLUGINS=""
for d in /usr/lib/x86_64-linux-gnu/qt6/plugins /usr/lib/qt6/plugins; do
    [[ -d "$d/platforms" ]] && QT_PLUGINS="$d" && break
done
[[ -n "$QT_PLUGINS" ]] || { echo "Qt6 플러그인 폴더를 찾지 못했습니다 (qt6-qpa-plugins 설치 확인)"; exit 1; }

rm -rf "$PKG_DIR"
mkdir -p "$PKG_DIR/lib" "$PKG_DIR/plugins"
install -m 755 "$BUILD_DIR/$APP" "$PKG_DIR/$APP"
strip "$PKG_DIR/$APP"

# 필수: xcb 플랫폼, SVG 아이콘 엔진(아이콘 574개가 SVG). 나머지는 있으면 넣는다.
PLUGINS_REQUIRED=(platforms/libqxcb.so iconengines/libqsvgicon.so)
PLUGINS_OPTIONAL=(imageformats/libqsvg.so imageformats/libqjpeg.so imageformats/libqgif.so
                  imageformats/libqico.so platforminputcontexts/libcomposeplatforminputcontextplugin.so
                  platforminputcontexts/libibusplatforminputcontextplugin.so)
for p in "${PLUGINS_REQUIRED[@]}"; do
    [[ -f "$QT_PLUGINS/$p" ]] || { echo "필수 Qt 플러그인 없음: $QT_PLUGINS/$p"; exit 1; }
done
for p in "${PLUGINS_REQUIRED[@]}" "${PLUGINS_OPTIONAL[@]}" $(cd "$QT_PLUGINS" && ls xcbglintegrations/*.so 2>/dev/null); do
    if [[ -f "$QT_PLUGINS/$p" ]]; then
        mkdir -p "$PKG_DIR/plugins/$(dirname "$p")"
        cp -L "$QT_PLUGINS/$p" "$PKG_DIR/plugins/$p"
    fi
done

# ---- 3. 공유 라이브러리 (ldd로 의존성을 끝까지 따라가며 넣을 것만 복사) -------------------------
bundle_ok() {
    # xcb는 본체(libxcb.so.1)만 시스템 것을 쓰고 확장 라이브러리는 모두 넣는다 — 데스크톱에 따라
    # xinput·icccm 등이 없어 'xcb 플랫폼 플러그인을 읽지 못함' 오류가 났다(README 문제 해결 항목).
    # libgomp는 libb2가 쓰는 OpenMP 런타임으로, 기본 설치에 없을 수 있다.
    case "$1" in
        libQt6*|libicu*|libmodbus*|libjsoncpp*|libdouble-conversion*|libmd4c*|libpcre2-16*|libb2*| \
        libgomp*|libxcb-*|libxkbcommon*) return 0 ;;
    esac
    return 1
}
mapfile -t todo < <(find "$PKG_DIR/$APP" "$PKG_DIR/plugins" -type f)
declare -A seen=()
while ((${#todo[@]})); do
    f="${todo[0]}"; todo=("${todo[@]:1}")
    while read -r soname path; do
        [[ -n "$path" && -z "${seen[$soname]:-}" ]] || continue
        seen[$soname]=1
        if bundle_ok "$soname"; then
            cp -L "$path" "$PKG_DIR/lib/$soname"
            todo+=("$PKG_DIR/lib/$soname")
        fi
    done < <(ldd "$f" | awk '$2 == "=>" && $3 ~ /^\// {print $1, $3}')
done

# 묶은 파일이 서로를 먼저 찾도록 RUNPATH 지정 (실행 스크립트 없이 실행해도 동작)
patchelf --set-rpath '$ORIGIN/lib' "$PKG_DIR/$APP"
find "$PKG_DIR/plugins" -name '*.so' -exec patchelf --set-rpath '$ORIGIN/../../lib' {} \;
find "$PKG_DIR/lib" -name '*.so*' -type f -exec patchelf --set-rpath '$ORIGIN' {} \;

cat > "$PKG_DIR/qt.conf" <<'EOF'
[Paths]
Prefix = .
Plugins = plugins
EOF

# ---- 4. 실행 스크립트 (사용자 매뉴얼: 압축을 풀고 kr_gcs_user_interface.sh 실행) ---------------
cat > "$PKG_DIR/$APP.sh" <<'EOF'
#!/bin/bash
# KR_GCS_user_interface 실행 (우분투 22.04)
DIR="$(cd "$(dirname "$(readlink -f "${BASH_SOURCE[0]}")")" && pwd)"
# 압축 프로그램이 실행 권한을 잃은 경우 복구
[[ -x "$DIR/kr_gcs_user_interface" ]] || chmod +x "$DIR/kr_gcs_user_interface" 2>/dev/null
export LD_LIBRARY_PATH="$DIR/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
export QT_PLUGIN_PATH="$DIR/plugins"
export QT_QPA_PLATFORM="${QT_QPA_PLATFORM:-xcb}"   # Wayland 세션에서도 XWayland로 실행
if ! id -nG | grep -qw dialout; then
    echo "[안내] 시리얼 포트(/dev/ttyUSB*)를 쓰려면 dialout 그룹이 필요합니다:"
    echo "       sudo usermod -aG dialout \$USER   (실행 후 다시 로그인)"
fi
exec "$DIR/kr_gcs_user_interface" "$@"
EOF
chmod 755 "$PKG_DIR/$APP.sh"

# ---- 5. 확인: 묶음 안에서 풀리지 않는 라이브러리가 없어야 한다 ------------------------------------
missing=$(find "$PKG_DIR" -type f \( -name "$APP" -o -name '*.so*' \) -exec ldd {} \; 2>/dev/null | grep "not found" | sort -u || true)
if [[ -n "$missing" ]]; then
    echo "풀리지 않는 라이브러리:"; echo "$missing"; exit 1
fi
echo "시스템에서 가져다 쓰는 라이브러리 (받는 PC에 있어야 함):"
find "$PKG_DIR" -type f \( -name "$APP" -o -name '*.so*' \) -exec ldd {} \; 2>/dev/null \
    | awk '$2 == "=>" && $3 ~ /^\// && $3 !~ /'"$PKG_NAME"'/ {print "  " $1}' | sort -u

# ---- 6. 7z ---------------------------------------------------------------------------------
(cd "$OUT_DIR" && rm -f "$PKG_NAME.7z" && 7z a -t7z -mx=9 "$PKG_NAME.7z" "$PKG_NAME" > /dev/null)
echo "완료: $OUT_DIR/$PKG_NAME.7z ($(du -h "$OUT_DIR/$PKG_NAME.7z" | cut -f1)), 폴더 $(du -sh "$PKG_DIR" | cut -f1)"
