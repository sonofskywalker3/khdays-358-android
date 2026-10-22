/* khdays-updates-test: the release versions the update check compares, and the latest release's
 * tag and installer read from GitHub's answer (host/app/release_version.h). */
#include "../host/app/release_version.h"

#include <stdio.h>
#include <string.h>

static int failures;

#define CHECK(c)                                                                                                       \
    do {                                                                                                               \
        if (!(c)) {                                                                                                    \
            fprintf(stderr, "%s:%d: CHECK(%s) failed\n", __FILE__, __LINE__, #c);                                      \
            ++failures;                                                                                                \
        }                                                                                                              \
    } while (0)

static void test_versions(void)
{
    CHECK(khdays_version_valid("0.1.0") && khdays_version_valid("v0.1.0") && khdays_version_valid("V2"));
    CHECK(khdays_version_valid("1.2") && khdays_version_valid("1.2.3-rc1") && khdays_version_valid("1.2.3+abc"));
    CHECK(!khdays_version_valid("") && !khdays_version_valid("v") && !khdays_version_valid("latest"));
    CHECK(!khdays_version_valid("1.2.3.4") && !khdays_version_valid("1..2") && !khdays_version_valid("1.2.3-"));
    CHECK(!khdays_version_valid(NULL));

    CHECK(khdays_version_compare("v0.1.0", "0.1.0") == 0); /* the tag's v and the build's none */
    CHECK(khdays_version_compare("v0.1.1", "0.1.0") > 0);
    CHECK(khdays_version_compare("v0.2.0", "0.1.9") > 0);
    CHECK(khdays_version_compare("v0.10.0", "0.9.0") > 0); /* numbers, not letters */
    CHECK(khdays_version_compare("v1", "0.9.9") > 0);
    CHECK(khdays_version_compare("v0.1", "0.1.0") == 0); /* missing parts count 0 */
    CHECK(khdays_version_compare("v0.1.0", "0.1.1") < 0);
    CHECK(khdays_version_compare("v0.2.0-rc1", "0.2.0") < 0); /* a pre-release is the older */
    CHECK(khdays_version_compare("v0.2.0-rc1", "0.1.0") > 0);
    CHECK(khdays_version_compare("v0.1.0+abc", "0.1.0") == 0);
    CHECK(khdays_version_compare("nightly", "0.1.0") < 0); /* no version: older than any */
    CHECK(khdays_version_compare("0.1.0", "nightly") > 0);
}

static void test_answer(void)
{
    char tag[64];
    /* GitHub's answer for the latest release, shortened: the tag among the other fields */
    static const char answer[] =
        "{\n  \"url\": \"https://api.github.com/repos/x/y/releases/1\",\n"
        "  \"html_url\": \"https://github.com/x/y/releases/tag/v0.1.1\",\n  \"id\": 1,\n"
        "  \"author\": {\"login\": \"x\"},\n  \"tag_name\" : \"v0.1.1\",\n  \"draft\": false\n}";
    CHECK(khdays_release_tag(answer, tag, sizeof(tag)) && strcmp(tag, "v0.1.1") == 0);
    CHECK(khdays_release_tag("{\"tag_name\":\"v2.0.0-rc1\"}", tag, sizeof(tag)) && strcmp(tag, "v2.0.0-rc1") == 0);

    /* none, or none that is a version's characters (it goes into text and a web address) */
    CHECK(!khdays_release_tag("{\"name\":\"v0.1.1\"}", tag, sizeof(tag)));
    CHECK(!khdays_release_tag("{\"tag_name\":\"v0.1.1 <b>\"}", tag, sizeof(tag)));
    CHECK(!khdays_release_tag("{\"tag_name\":\"../../evil\"}", tag, sizeof(tag)));
    CHECK(!khdays_release_tag("{\"tag_name\":\"latest\"}", tag, sizeof(tag)));
    CHECK(!khdays_release_tag("{\"tag_name\":\"\"}", tag, sizeof(tag)));
    CHECK(!khdays_release_tag("{\"tag_name\":\"v0.1.1", tag, sizeof(tag))); /* cut short */
    CHECK(!khdays_release_tag("{\"tag_name\": 7}", tag, sizeof(tag)));
    CHECK(!khdays_release_tag(NULL, tag, sizeof(tag)));
    CHECK(!khdays_release_tag("{\"tag_name\":\"v0.1.1\"}", tag, 6)); /* it does not fit */
    CHECK(khdays_release_tag("{\"tag_name\":\"v0.1.1\"}", tag, 7) && strcmp(tag, "v0.1.1") == 0);
}

static void test_installer(void)
{
    KhdaysReleaseFile f;
    /* GitHub's answer, shortened: two files, the installer the second, with a nested uploader */
    static const char answer[] =
        "{\"url\":\"https://api.github.com/repos/x/y/releases/1\",\"tag_name\":\"v0.1.4\",\"author\":{\"login\":\"x\","
        "\"id\":1},\"body\":\"notes with \\\"quotes\\\" and [brackets] {braces}\",\"assets\":[{\"name\":\"notes.txt\","
        "\"size\":12,\"digest\":\"sha256:"
        "0000000000000000000000000000000000000000000000000000000000000000\",\"browser_download_url\":"
        "\"https://github.com/x/y/releases/download/v0.1.4/notes.txt\"},{\"url\":\"https://api.github.com/a/2\","
        "\"id\":2,\"name\":\"KH358-2-Days-Port-Setup-0.1.4.exe\",\"label\":null,\"uploader\":{\"login\":\"x\","
        "\"site_admin\":false},\"content_type\":\"application/x-msdownload\",\"state\":\"uploaded\","
        "\"size\":28812813,\"digest\":\"sha256:"
        "6D5C19706E5FA386F164A338DADD03A6AFE791F37F35139ADF13DDADA6E149FF\",\"download_count\":0,"
        "\"browser_download_url\":\"https://github.com/x/y/releases/download/v0.1.4/"
        "KH358-2-Days-Port-Setup-0.1.4.exe\"}],\"zipball_url\":\"https://api.github.com/z\"}";
    CHECK(khdays_release_installer(answer, &f));
    CHECK(strcmp(f.name, "KH358-2-Days-Port-Setup-0.1.4.exe") == 0 && f.size == 28812813);
    CHECK(strcmp(f.url, "https://github.com/x/y/releases/download/v0.1.4/KH358-2-Days-Port-Setup-0.1.4.exe") == 0);
    CHECK(strcmp(f.sha256, "6d5c19706e5fa386f164a338dadd03a6afe791f37f35139adf13ddada6e149ff") == 0); /* lower case */

    /* none: no installer, one without its digest or with a broken one, a cut-short answer */
    CHECK(!khdays_release_installer("{\"tag_name\":\"v0.1.4\",\"assets\":[]}", &f));
    CHECK(!khdays_release_installer("{\"assets\":[{\"name\":\"KH358-2-Days-Port-Setup-0.1.4.exe\",\"size\":5,"
                                    "\"browser_download_url\":\"https://github.com/a\"}]}",
                                    &f));
    CHECK(!khdays_release_installer("{\"assets\":[{\"name\":\"KH358-2-Days-Port-Setup-0.1.4.exe\",\"size\":5,"
                                    "\"digest\":\"sha256:12ab\",\"browser_download_url\":\"https://github.com/a\"}]}",
                                    &f));
    CHECK(!khdays_release_installer("{\"assets\":[{\"name\":\"KH358-2-Days-Port-Setup-0.1.4.zip\",\"size\":5,"
                                    "\"digest\":\"sha256:"
                                    "6d5c19706e5fa386f164a338dadd03a6afe791f37f35139adf13ddada6e149ff\","
                                    "\"browser_download_url\":\"https://github.com/a\"}]}",
                                    &f));
    CHECK(!khdays_release_installer("{\"assets\":[{\"name\":\"KH358-2-Days-Port-Setup-0.1.4.exe\"", &f));
    CHECK(!khdays_release_installer("[]", &f) && !khdays_release_installer(NULL, &f));
}

int main(void)
{
    test_versions();
    test_answer();
    test_installer();
    if (failures != 0) {
        fprintf(stderr, "%d check(s) failed\n", failures);
        return 1;
    }
    printf("updates: all checks passed\n");
    return 0;
}
