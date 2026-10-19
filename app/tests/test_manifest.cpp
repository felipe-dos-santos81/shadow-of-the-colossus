// Scene-manifest parser tests: the W1.1 schema (S3 arena plan) — a
// line-oriented list of placed instances plus an optional spawn, strict
// finite-float numbers, and failures that name the file and line. Fixture text
// goes to a temp path through the same pattern as test_asset_io.cpp.
#include <doctest.h>

#include <unistd.h>

#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>
#include <system_error>
#include <vector>

#include "manifest.h"

namespace {

// A scratch directory that deletes itself, holding one fixture manifest.
class TempFixtureDir {
public:
    TempFixtureDir()
        : path_(std::filesystem::temp_directory_path() /
                ("sotc-manifest-fixture-" + std::to_string(::getpid()))) {
        std::error_code ec;
        std::filesystem::create_directories(path_, ec);
        if (ec) {
            throw std::runtime_error("cannot create fixture dir " + path_.string() + ": " +
                                     ec.message());
        }
    }
    ~TempFixtureDir() {
        std::error_code ec;
        std::filesystem::remove_all(path_, ec);  // best effort
    }
    TempFixtureDir(const TempFixtureDir&) = delete;
    TempFixtureDir& operator=(const TempFixtureDir&) = delete;

    const std::filesystem::path& path() const { return path_; }

private:
    std::filesystem::path path_;
};

std::filesystem::path write_manifest(const TempFixtureDir& dir, const std::string& text) {
    const std::filesystem::path path = dir.path() / "fixture.manifest";
    std::ofstream out(path);
    REQUIRE(out.good());
    out << text;
    return path;
}

std::string join_tokens(const std::vector<std::string>& tokens) {
    std::string line;
    for (const std::string& token : tokens) {
        if (!line.empty()) line.push_back(' ');
        line += token;
    }
    return line;
}

// The message parse_manifest threw for `path` — or "" when it did not throw,
// so the caller's CHECKs fail instead of passing vacuously.
std::string manifest_error(const std::filesystem::path& path) {
    try {
        parse_manifest(path);
    } catch (const std::runtime_error& error) {
        return error.what();
    }
    return "";
}

}  // namespace

TEST_CASE("a manifest with one instance and one spawn parses with exact values") {
    TempFixtureDir dir;
    const std::string text =
        "instance models/objects/props/_cmn/obj_plank_01.glb"
        " 1.5 -2 3 0 0 0 1 2 2 2\n"
        "spawn 10 0.25 -4.5 90\n";
    const std::filesystem::path path = write_manifest(dir, text);

    const SceneManifest manifest = parse_manifest(path);

    REQUIRE(manifest.instances.size() == 1);
    const ManifestInstance& instance = manifest.instances[0];
    CHECK(instance.glbRelPath == "models/objects/props/_cmn/obj_plank_01.glb");
    CHECK(instance.position.x == doctest::Approx(1.5f));
    CHECK(instance.position.y == doctest::Approx(-2.0f));
    CHECK(instance.position.z == doctest::Approx(3.0f));
    CHECK(instance.rotation.x == doctest::Approx(0.0f));
    CHECK(instance.rotation.y == doctest::Approx(0.0f));
    CHECK(instance.rotation.z == doctest::Approx(0.0f));
    CHECK(instance.rotation.w == doctest::Approx(1.0f));
    CHECK(instance.scale.x == doctest::Approx(2.0f));
    CHECK(instance.scale.y == doctest::Approx(2.0f));
    CHECK(instance.scale.z == doctest::Approx(2.0f));
    CHECK(manifest.hasSpawn);
    CHECK(manifest.spawnPosition.x == doctest::Approx(10.0f));
    CHECK(manifest.spawnPosition.y == doctest::Approx(0.25f));
    CHECK(manifest.spawnPosition.z == doctest::Approx(-4.5f));
    CHECK(manifest.spawnYawDeg == doctest::Approx(90.0f));
}

TEST_CASE("comments and blank lines are skipped") {
    TempFixtureDir dir;
    const std::filesystem::path path = write_manifest(
        dir,
        "# valus arena fixture\n"
        "\n"
        "   \t \n"
        "# another comment\n"
        "instance models/shadow/levels/arena_01/_cmn/arena_01.glb 0 0 0 0 0 0 1 1 1 1  # trailing\n"
        "\tspawn 1 2 3 45\n"
        "# end\n");

    const SceneManifest manifest = parse_manifest(path);

    REQUIRE(manifest.instances.size() == 1);
    CHECK(manifest.instances[0].glbRelPath == "models/shadow/levels/arena_01/_cmn/arena_01.glb");
    CHECK(manifest.hasSpawn);
    CHECK(manifest.spawnPosition.y == doctest::Approx(2.0f));
    CHECK(manifest.spawnYawDeg == doctest::Approx(45.0f));
}

TEST_CASE("a manifest without spawn reports no spawn at the origin") {
    TempFixtureDir dir;
    const std::string text =
        "instance models/objects/props/_cmn/obj_plank_01.glb"
        " 0 0 0 0 0 0 1 1 1 1\n";
    const std::filesystem::path path = write_manifest(dir, text);

    const SceneManifest manifest = parse_manifest(path);

    REQUIRE(manifest.instances.size() == 1);
    CHECK_FALSE(manifest.hasSpawn);
    CHECK(manifest.spawnPosition.x == 0.0f);
    CHECK(manifest.spawnPosition.y == 0.0f);
    CHECK(manifest.spawnPosition.z == 0.0f);
}

TEST_CASE("a wrong token count fails naming the file and line") {
    TempFixtureDir dir;

    // Line 3: an `instance` with too few numbers (a comment and a blank line
    // precede it, so the reported line must be the real one).
    const std::filesystem::path instance_path = write_manifest(
        dir, "# header\n\ninstance models/objects/props/_cmn/obj_plank_01.glb 0 0 0\n");
    CHECK(manifest_error(instance_path).starts_with(instance_path.string() + ":3:"));

    // A `spawn` with too many tokens, on line 1.
    const std::filesystem::path spawn_path = write_manifest(dir, "spawn 1 2 3 4 5\n");
    CHECK(manifest_error(spawn_path).starts_with(spawn_path.string() + ":1:"));
}

TEST_CASE("a non-finite or partial number fails with its line number") {
    TempFixtureDir dir;

    const std::vector<std::string> instance_tokens = {"instance", "models/x.glb", "0", "0", "0",
                                                      "0",        "0",           "0", "1", "1",
                                                      "1",        "1"};
    const std::vector<std::string> spawn_tokens = {"spawn", "0", "0", "0", "0"};

    // The pristine fixture parses: the loop below only ever changes one token,
    // so a failure there is the bad token's, never a bad fixture's.
    CHECK(parse_manifest(write_manifest(dir, "# header\n" + join_tokens(instance_tokens) + "\n" +
                                                join_tokens(spawn_tokens) + "\n"))
              .instances.size() == 1);

    for (const std::string& bad :
         {std::string("nan"), std::string("inf"), std::string("-inf"), std::string("1.5x"),
          std::string("1e999")}) {
        // Every numeric field of both directives: an instance's 10 and a
        // spawn's 4, with the offending line always on line 2.
        for (int field = 0; field < 10; ++field) {
            std::vector<std::string> tokens = instance_tokens;
            tokens[2 + field] = bad;
            const std::string text = "# header\n" + join_tokens(tokens) + "\n";
            const std::filesystem::path path = write_manifest(dir, text);
            CAPTURE(bad);
            CAPTURE(field);
            const std::string message = manifest_error(path);
            CHECK(message.starts_with(path.string() + ":2:"));
            CHECK(message.find("'" + bad + "'") != std::string::npos);
        }
        for (int field = 0; field < 4; ++field) {
            std::vector<std::string> tokens = spawn_tokens;
            tokens[1 + field] = bad;
            const std::string text = "# header\n" + join_tokens(tokens) + "\n";
            const std::filesystem::path path = write_manifest(dir, text);
            CAPTURE(bad);
            CAPTURE(field);
            const std::string message = manifest_error(path);
            CHECK(message.starts_with(path.string() + ":2:"));
            CHECK(message.find("'" + bad + "'") != std::string::npos);
        }
    }
}

TEST_CASE("an unknown directive fails naming it") {
    TempFixtureDir dir;
    const std::filesystem::path path = write_manifest(dir, "foo bar 1 2 3\n");

    const std::string message = manifest_error(path);
    CHECK(message.starts_with(path.string() + ":1:"));
    CHECK(message.find("'foo'") != std::string::npos);
}

TEST_CASE("a manifest that cannot be opened throws") {
    TempFixtureDir dir;
    CHECK_THROWS_AS(parse_manifest(dir.path() / "no_such.manifest"), std::runtime_error);
    CHECK_THROWS_AS(parse_manifest(dir.path()), std::runtime_error);  // a directory, not a file
}
