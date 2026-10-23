# FBX in (a mod's models): ufbx v0.23.1 (tag object
# 35a37776ee1163a74609f4ee75d162b7ada099ac), MIT or Unlicense. Only the three
# files the port compiles and its license are fetched, at the commit
# THIRD_PARTY_NOTICES.md records, each checked by its SHA-256 -- the
# repository's 67 MB of test data are not wanted. Sets ufbx_SOURCE_DIR.
set(KHDAYS_UFBX_COMMIT 26a482ae66871d7de36eb722aa060bce95bce274)
set(ufbx_SOURCE_DIR "${CMAKE_BINARY_DIR}/_deps/ufbx-${KHDAYS_UFBX_COMMIT}")
foreach(entry
        "ufbx.c=4a956c26a708e40d82ecb0aeaee54da709d5ba7c144c9642174970059bda7e1d"
        "ufbx.h=f787be529af577efc04f3e3e735844a08556718dbb613d1211c2b313d72895dc"
        "LICENSE=0dd48ebadf52273c736256325c8f078c03c8bb4facee22a4122de0ad3f615391")
    string(REPLACE "=" ";" pair "${entry}")
    list(GET pair 0 name)
    list(GET pair 1 hash)
    set(target "${ufbx_SOURCE_DIR}/${name}")
    if(EXISTS "${target}")
        file(SHA256 "${target}" have)
        if(have STREQUAL hash)
            continue()
        endif()
    endif()
    file(DOWNLOAD "https://raw.githubusercontent.com/ufbx/ufbx/${KHDAYS_UFBX_COMMIT}/${name}" "${target}"
         EXPECTED_HASH SHA256=${hash} STATUS status TLS_VERIFY ON)
    list(GET status 0 code)
    if(NOT code EQUAL 0)
        list(GET status 1 why)
        message(FATAL_ERROR "ufbx: cannot fetch ${name}: ${why}")
    endif()
endforeach()
