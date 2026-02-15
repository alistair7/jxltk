vcpkg_from_github(
    OUT_SOURCE_PATH SOURCE_PATH
    REPO alistair7/jxltk
    REF "v${VERSION}"
    SHA512 f9779e9765063f4e2483d7de58d6427fba7ea5101c403cc55aebbd5a239b72ac9e922bc819b29b981940f74012921d69abadd6621b1849e5c3e7fa1f576985ab
    PATCHES fix-windows-build.patch
    HEAD_REF main
)

vcpkg_cmake_configure(
    SOURCE_PATH "${SOURCE_PATH}"
    OPTIONS -DBUILD_SHARED_LIBS=OFF
    OPTIONS_RELEASE -DCMAKE_BUILD_TYPE=Release
    OPTIONS_DEBUG -DCMAKE_BUILD_TYPE=Debug
)

vcpkg_cmake_install()

vcpkg_install_copyright(FILE_LIST "${SOURCE_PATH}/LICENSE")

# This isn't a library
set(VCPKG_POLICY_EMPTY_INCLUDE_FOLDER enabled)
set(VCPKG_POLICY_ALLOW_EXES_IN_BIN enabled)
