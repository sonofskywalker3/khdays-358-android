// The port's mods' log as the console reads it, and the reloads it asks for.
#include "test.h"

#include "core/mods/mods_log.h"

#include <filesystem>
#include <fstream>
#include <iterator>

using namespace studio;
namespace fs = std::filesystem;

namespace {

std::string read(const fs::path& file) {
    std::ifstream in(file, std::ios::binary);
    return {std::istreambuf_iterator<char>(in), {}};
}

void append(const fs::path& file, const std::string& text) {
    std::ofstream out(file, std::ios::binary | std::ios::app);
    out << text;
}

}  // namespace

TEST(mods_log_lines) {
    ModLogLine l;
    CHECK(parse_mod_log_line("12.345 example.where print scripts/main.lua:8 mission 10 at 1.00", &l));
    CHECK(l.seconds > 12.3 && l.seconds < 12.4 && l.mod == "example.where" && l.kind == "print" &&
          l.where == "scripts/main.lua:8" && l.text == "mission 10 at 1.00");
    CHECK(parse_mod_log_line("0.001 example.where loaded - started, api 1", &l));
    CHECK(l.where == "-" && l.text == "started, api 1");
    CHECK(parse_mod_log_line("1.000 a.b print scripts/main.lua:1 ", &l) && l.text.empty());  // an empty print
    CHECK(!parse_mod_log_line("", &l));
    CHECK(!parse_mod_log_line("not a number a b c", &l));
    CHECK(!parse_mod_log_line("1.0 only three", &l));
}

TEST(mods_log_follow) {
    const fs::path dir = studio_test::scratch("mods_log");
    const fs::path file = dir / "mods.log";
    ModsLog log(file);
    CHECK(!log.poll());  // no file yet
    studio_test::write(file, "0.001 test.a loaded - started, api 1\n0.002 test.b refused - needs mod interface 2\n");
    CHECK(log.poll() && log.lines().size() == 2);
    CHECK(log.states().at("test.a") == "loaded" && log.states().at("test.b") == "refused");
    CHECK(!log.poll());  // nothing new
    append(file, "3.000 test.a error scripts/main.lua:3 attempt to index a nil value\n3.000 test.a error - stack");
    CHECK(log.poll() && log.lines().size() == 3);  // the line not ended yet waits
    append(file, " traceback:\n3.000 test.a stopped - the error above\n");
    CHECK(log.poll() && log.lines().size() == 5 && log.lines()[3].text == "stack traceback:");
    CHECK(log.states().at("test.a") == "stopped");
    // a new run's log, shorter: read from its start
    studio_test::write(file, "0.001 test.c loaded - started, api 1\n");
    CHECK(log.poll() && log.lines().size() == 1 && log.states().count("test.a") == 0);
}

TEST(mods_reload_asked) {
    const fs::path saves = studio_test::scratch("mods_reload");
    std::string why;
    CHECK(ask_mod_reload(saves, "test.a", &why));
    CHECK(ask_mod_reload(saves, "*", &why));
    CHECK(read(saves / "mods" / "reload.txt") == "test.a\n*\n");
}
