/* The port's release versions as its tags write them ("v0.1.0"), and the latest release's tag in
 * GitHub's answer: pure functions, for the update check (updates.c; tests/updates_test.c). */
#ifndef KHDAYS_HOST_RELEASE_VERSION_H
#define KHDAYS_HOST_RELEASE_VERSION_H

#include <stddef.h>

/* Whether `s` is a version: "1", "1.2" or "1.2.3", with a leading "v" or not, then nothing, a
 * pre-release ("-rc1") or build metadata ("+abc"). */
int khdays_version_valid(const char *s);

/* Below zero when version `a` is older than `b`, zero when they are the same, above zero when it
 * is newer. Missing parts count 0; at equal numbers a pre-release is the older, and two pre-releases
 * go by their texts with numbers as numbers (beta10 after beta9). A text that is no version counts
 * older than any version. */
int khdays_version_compare(const char *a, const char *b);

/* The latest release's tag in GitHub's JSON answer (its "tag_name") into `tag`, at most `size`
 * bytes with its NUL: 1, or 0 when the answer has none, or none that is a version. */
int khdays_release_tag(const char *json, char *tag, size_t size);

/* A file of the release, as GitHub's answer lists it among its "assets". */
typedef struct KhdaysReleaseFile {
    char name[128];
    char url[1024];  /* its "browser_download_url" */
    char sha256[65]; /* its "digest" without "sha256:", 64 lowercase hex digits */
    long long size;  /* its "size", in bytes */
} KhdaysReleaseFile;

/* The release's installer in GitHub's JSON answer: the file named KH358-2-Days-Port-Setup-<...>.exe,
 * with its address, size and SHA-256. 1, or 0 when the answer lists none, or none with a digest (a
 * download that could not be checked is not run). */
int khdays_release_installer(const char *json, KhdaysReleaseFile *out);

/* The Android port's newest release in GitHub's answer for a repository's releases (a JSON array):
 * of those tagged "android-<version>" or "android-<version>-beta<n>" that list a "khdays-358-android-<...>.apk" with a digest, the
 * highest version. That version (the tag without "android-", at most `size` bytes with its NUL) into
 * `version` and its APK into `out`: 1, or 0 when there is none. */
int khdays_release_newest_android(const char *json, char *version, size_t size, KhdaysReleaseFile *out);

#endif
