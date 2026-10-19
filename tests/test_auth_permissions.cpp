// M2 tests: SHA-256, users, sessions, permissions, auth handler. Real data only.
#include <atomic>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <set>
#include <sstream>
#include <thread>
#include <vector>

#include "server/AuthHandler.h"
#include "server/PermissionEngine.h"
#include "server/SessionManager.h"
#include "server/Sha256.h"
#include "server/UserManager.h"

using namespace FSP;
using namespace FSP::Auth;
namespace fs = std::filesystem;

static int g_checks = 0, g_fail = 0;
#define CHECK(c)                                                              \
    do {                                                                      \
        ++g_checks;                                                           \
        if (!(c)) {                                                           \
            ++g_fail;                                                         \
            std::fprintf(stderr, "FAIL %s:%d  %s\n", __FILE__, __LINE__, #c); \
        }                                                                     \
    } while (0)

static std::string slurp(const fs::path& p) {
    std::ifstream f(p, std::ios::binary);
    std::stringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

static void testSha256() {
    CHECK(sha256Hex("") == "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
    CHECK(sha256Hex("abc") == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
    CHECK(sha256Hex("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq") ==
          "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1");
    // 64-byte message forces a second padding block.
    CHECK(sha256Hex(std::string(64, 'a')) ==
          "ffe054fe7ae0cb6dc65c3af9b61d5209f439851db43d0ba5997337df154668eb");
}

static void testUsers(const fs::path& dir) {
    const std::string db = (dir / "users.json").string();
    uint32_t aliceId = 0, bobId = 0;
    {
        UserManager um(db);
        CHECK(um.registerUser("alice", "same-password", "", &aliceId) == StatusCode::CREATED);
        CHECK(um.registerUser("bob", "same-password", "", &bobId) == StatusCode::CREATED);
        CHECK(aliceId == 1 && bobId == 2);
        CHECK(um.registerUser("alice", "other-password") == StatusCode::CONFLICT);
        CHECK(um.registerUser("a!", "long-enough") == StatusCode::BAD_REQUEST);
        CHECK(um.registerUser("carol", "short") == StatusCode::BAD_REQUEST);
        CHECK(um.registerUser("../etc", "long-enough") == StatusCode::BAD_REQUEST);

        CHECK(um.verifyLogin("alice", "same-password").has_value());
        CHECK(!um.verifyLogin("alice", "wrong-password").has_value());
        CHECK(!um.verifyLogin("nobody", "same-password").has_value());

        auto a = um.findById(aliceId), b = um.findById(bobId);
        CHECK(a && b);
        CHECK(a->salt != b->salt);                    // same password, different salts...
        CHECK(a->password_hash != b->password_hash);  // ...so different hashes
        CHECK(a->password_hash == sha256Hex(a->salt + "same-password"));
        CHECK(a->salt.size() == 32 && a->password_hash.size() == 64);
    }
    CHECK(slurp(db).find("same-password") == std::string::npos);  // never stored in clear

    UserManager reloaded(db);  // persistence
    CHECK(reloaded.verifyLogin("bob", "same-password").has_value());
    CHECK(reloaded.findById(bobId)->username == "bob");
    CHECK(reloaded.registerUser("carol", "long-enough") == StatusCode::CREATED);
    CHECK(reloaded.findById(3).has_value());

    std::ofstream(dir / "corrupt.json") << "{not json\n";
    bool threw = false;
    try {
        UserManager bad((dir / "corrupt.json").string());
    } catch (const std::runtime_error&) {
        threw = true;
    }
    CHECK(threw);  // corrupt DB must not silently load as empty
}

// PROBE-2
static void testSessions() {
    std::time_t now = 1'000'000;
    SessionManager sm([&] { return now; });

    std::string tokA = sm.create(7), tokB = sm.create(8);
    CHECK(tokA != tokB && tokA.size() == 36 && tokA[14] == '4');
    CHECK(sm.validate(tokA)->user_id == 7);  // each token maps to its own user only
    CHECK(sm.validate(tokB)->user_id == 8);

    std::string forged = tokA;
    forged[0] = forged[0] == 'a' ? 'b' : 'a';
    CHECK(!sm.validate(forged).has_value());
    CHECK(!sm.validate("").has_value());

    sm.destroy(tokA);  // logged-out token
    CHECK(!sm.validate(tokA).has_value());

    now += 20 * 60;  // activity refreshes the timeout
    CHECK(sm.validate(tokB).has_value());
    now += 20 * 60;
    CHECK(sm.validate(tokB).has_value());
    CHECK(sm.setCwd(tokB, "/docs"));
    CHECK(sm.validate(tokB)->cwd == "/docs");

    now += 31 * 60;  // idle past 30 min
    CHECK(!sm.validate(tokB).has_value());
    CHECK(!sm.setCwd(tokB, "/x"));
}

// PROBE-3
static void testPermissions(const fs::path& dir) {
    UserManager um((dir / "perm_users.json").string());
    uint32_t admin = 0, bob = 0, carol = 0, dave = 0;
    um.registerUser("admin", "password1", "", &admin);
    um.registerUser("bob", "password1", "", &bob);
    um.registerUser("carol", "password1", "", &carol);
    um.registerUser("dave", "password1", "", &dave);
    CHECK(admin == ADMIN_USER_ID);
    CHECK(um.setGroup(bob, carol, bob) == StatusCode::FORBIDDEN);  // only the admin manages groups
    CHECK(um.findById(carol)->group_id == carol);                  // ...and nothing changed
    CHECK(um.setGroup(admin, carol, 0) == StatusCode::BAD_REQUEST);
    CHECK(um.setGroup(admin, carol, bob) == StatusCode::OK);  // carol joins bob's group
    CHECK(um.setGroup(admin, 99, 5) == StatusCode::NOT_FOUND);

    const std::string db = (dir / "permissions.json").string();
    const AccessType R = AccessType::READ, W = AccessType::WRITE, S = AccessType::SHARE;
    {
        PermissionEngine pe(db, um);
        CHECK(pe.registerPath("/bob/priv", bob, 0666, SharingLevel::PRIVATE) ==
              StatusCode::CREATED);
        CHECK(pe.registerPath("/bob/priv", bob) == StatusCode::CONFLICT);
        CHECK(pe.registerPath("/bob/pub", bob, 0644, SharingLevel::PUBLIC) == StatusCode::CREATED);
        CHECK(pe.registerPath("/bob/shr", bob, 0600, SharingLevel::SHARED) == StatusCode::CREATED);
        CHECK(pe.registerPath("/x", 99) == StatusCode::BAD_REQUEST);
        CHECK(pe.registerPath("/x", bob, 01777) == StatusCode::BAD_REQUEST);

        // PRIVATE: only owner (per owner bits) and admin; 0666 "other" bits must not leak.
        CHECK(pe.canAccess(bob, "/bob/priv", R) && pe.canAccess(bob, "/bob/priv", W));
        CHECK(pe.canAccess(bob, "/bob/priv", S));  // owner can always chmod/share (SRS section 5)
        CHECK(!pe.canAccess(carol, "/bob/priv", R));  // same group, still PRIVATE
        CHECK(!pe.canAccess(dave, "/bob/priv", R));
        CHECK(pe.canAccess(admin, "/bob/priv", W));

        // PUBLIC 0644: everyone reads; group (0644 -> group r--) reads; nobody else writes.
        CHECK(pe.canAccess(dave, "/bob/pub", R) && !pe.canAccess(dave, "/bob/pub", W));
        CHECK(pe.canAccess(carol, "/bob/pub", R) && !pe.canAccess(carol, "/bob/pub", W));
        CHECK(!pe.canAccess(dave, "/bob/pub", S));

        // SHARED 0600: only the ACL grants.
        CHECK(!pe.canAccess(dave, "/bob/shr", R));
        CHECK(pe.grantAcl(dave, "/bob/shr", dave, 1) ==
              StatusCode::FORBIDDEN);  // dave can't self-grant
        CHECK(pe.grantAcl(bob, "/bob/shr", dave, 0) == StatusCode::BAD_REQUEST);
        CHECK(pe.grantAcl(bob, "/bob/shr", dave, 1) == StatusCode::OK);  // owner, though 0600
        CHECK(pe.chmod(bob, "/bob/shr", 0700) == StatusCode::OK);
        CHECK(pe.canAccess(dave, "/bob/shr", R));
        CHECK(!pe.canAccess(dave, "/bob/shr", W) && !pe.canAccess(dave, "/bob/shr", S));
        CHECK(pe.revokeAcl(bob, "/bob/shr", dave) == StatusCode::OK);
        CHECK(!pe.canAccess(dave, "/bob/shr", R));

        // Owner of a read-only file (0400): reads and chmods, but cannot write.
        CHECK(pe.registerPath("/bob/ro", bob, 0400) == StatusCode::CREATED);
        CHECK(pe.canAccess(bob, "/bob/ro", R) && pe.canAccess(bob, "/bob/ro", S));
        CHECK(!pe.canAccess(bob, "/bob/ro", W));

        // Fail-closed: untracked path, unknown user.
        CHECK(!pe.canAccess(bob, "/nope", R));
        CHECK(!pe.canAccess(99, "/bob/pub", R));

        // chmod / chown / setSharingLevel
        CHECK(pe.chmod(dave, "/bob/pub", 0777) == StatusCode::FORBIDDEN);
        CHECK(pe.chmod(bob, "/bob/pub", 01777) == StatusCode::BAD_REQUEST);
        CHECK(pe.chmod(bob, "/nope", 0600) == StatusCode::NOT_FOUND);
        CHECK(pe.chmod(carol, "/bob/pub", 0777) == StatusCode::FORBIDDEN);  // group can't chmod
        CHECK(pe.chmod(bob, "/bob/pub", 0700) == StatusCode::OK);  // owner can, though 0644
        CHECK(!pe.canAccess(carol, "/bob/pub", W));
        CHECK(pe.canAccess(dave, "/bob/pub", R));  // still PUBLIC
        CHECK(pe.setSharingLevel(bob, "/bob/pub", SharingLevel::PRIVATE) == StatusCode::OK);
        CHECK(!pe.canAccess(dave, "/bob/pub", R));
        CHECK(pe.chown(bob, "/bob/pub", dave, dave) == StatusCode::FORBIDDEN);  // admin only
        CHECK(pe.chown(admin, "/bob/pub", dave, dave) == StatusCode::OK);
        CHECK(pe.canAccess(dave, "/bob/pub", W) && !pe.canAccess(bob, "/bob/pub", R));

        CHECK(pe.removePath("/bob/priv") && !pe.removePath("/bob/priv"));
        CHECK(!pe.canAccess(bob, "/bob/priv", R));
    }

    // Persistence: a fresh engine on the same file gives the same decisions.
    PermissionEngine again(db, um);
    CHECK(again.canAccess(dave, "/bob/pub", W) && !again.canAccess(bob, "/bob/pub", R));
    CHECK(again.get("/bob/shr")->perm_bits == 0700);
    CHECK(!again.get("/bob/priv").has_value());
}

static std::string tokenOf(const std::string& body) {
    auto m = jsonParseFlat(body);
    return m && m->count("token") ? m->at("token") : "";
}

static void testHandler(const fs::path& dir) {
    UserManager um((dir / "handler_users.json").string());
    SessionManager sm;
    PermissionEngine pe((dir / "handler_perms.json").string(), um);
    AuthHandler h(um, sm, &pe);
    const std::string pw =
        jsonEscape("p\"a\\ss1");  // quote + backslash survive the JSON round trip

    auto r = h.handle(Events::AUTH_REGISTER, "{\"username\":\"hieu\",\"password\":\"" + pw +
                                                 "\",\"full_name\":\"B\\u00f9i \xc4\x90\xe1\xbb\xa9"
                                                 "c Hi\xe1\xba\xbfu\"}");
    CHECK(r.status == StatusCode::CREATED && r.body == "{\"user_id\":1}");
    CHECK(um.findById(1)->full_name ==
          "B\xc3\xb9i \xc4\x90\xe1\xbb\xa9"
          "c Hi\xe1\xba\xbfu");

    // Registration also starts tracking the user's private home directory.
    auto home = pe.get("/home/hieu");
    CHECK(home && home->owner_id == 1 && home->perm_bits == 0755 &&
          home->sharing_level == SharingLevel::PRIVATE);
    h.handle(Events::AUTH_REGISTER, "{\"username\":\"other\",\"password\":\"another1\"}");
    CHECK(pe.canAccess(1, "/home/hieu", AccessType::WRITE));
    CHECK(!pe.canAccess(2, "/home/hieu", AccessType::READ));
    CHECK(pe.get("/home/other").has_value());
    CHECK(h.handle(Events::AUTH_REGISTER,
                   "{\"username\":\"u3\",\"password\":\"another1\","
                   "\"full_name\":\"" +
                       std::string(101, 'x') + "\"}")
              .status == StatusCode::BAD_REQUEST);
    r = h.handle(Events::AUTH_REGISTER, "{\"username\":\"hieu\",\"password\":\"another1\"}");
    CHECK(r.status == StatusCode::CONFLICT);
    r = h.handle(Events::AUTH_REGISTER, "{\"username\":\"x\",\"password\":\"another1\"}");
    CHECK(r.status == StatusCode::BAD_REQUEST);
    r = h.handle(Events::AUTH_REGISTER, "not json");
    CHECK(r.status == StatusCode::BAD_REQUEST);
    r = h.handle(Events::AUTH_LOGIN, "{\"username\":\"hieu\"}");
    CHECK(r.status == StatusCode::BAD_REQUEST);
    r = h.handle("AUTH_NOPE", "{}");
    CHECK(r.status == StatusCode::BAD_REQUEST);

    r = h.handle(Events::AUTH_LOGIN, "{\"username\":\"hieu\",\"password\":\"wrong-pass\"}");
    CHECK(r.status == StatusCode::UNAUTHORIZED);
    r = h.handle(Events::AUTH_LOGIN, "{\"username\":\"hieu\",\"password\":\"" + pw + "\"}");
    CHECK(r.status == StatusCode::OK);
    std::string tok = tokenOf(r.body);
    CHECK(tok.size() == 36 && sm.validate(tok)->user_id == 1);

    r = h.handle(Events::AUTH_LOGOUT, "{\"token\":\"forged-token\"}");
    CHECK(r.status == StatusCode::UNAUTHORIZED);
    r = h.handle(Events::AUTH_LOGOUT, "{\"token\":\"" + tok + "\"}");
    CHECK(r.status == StatusCode::OK);
    CHECK(!sm.validate(tok).has_value());
    r = h.handle(Events::AUTH_LOGOUT, "{\"token\":\"" + tok + "\"}");  // already logged out
    CHECK(r.status == StatusCode::UNAUTHORIZED);
}

static void testRandom() {
    std::set<std::string> seen;
    for (int i = 0; i < 200; ++i)
        seen.insert(randomHex(16));
    CHECK(seen.size() == 200);  // 128-bit values never repeat
    CHECK(randomHex(16).size() == 32 && randomHex(0).empty());
}

static void testLockout(const fs::path& dir) {
    std::time_t now = 5'000'000;
    UserManager um((dir / "lock_users.json").string(), [&] { return now; });
    um.registerUser("lockme", "correct-pw");
    um.registerUser("bystander", "correct-pw");

    for (int i = 0; i < UserManager::MAX_FAILED_LOGINS - 1; ++i)
        CHECK(!um.verifyLogin("lockme", "wrong-pw-" + std::to_string(i)).has_value());
    CHECK(!um.isLockedOut("lockme"));
    CHECK(um.verifyLogin("lockme", "correct-pw").has_value());  // success resets the counter
    for (int i = 0; i < UserManager::MAX_FAILED_LOGINS; ++i)
        um.verifyLogin("lockme", "wrong-pw-" + std::to_string(i));
    CHECK(um.isLockedOut("lockme"));
    CHECK(!um.verifyLogin("lockme", "correct-pw")
               .has_value());  // refused even with the right password
    CHECK(um.verifyLogin("bystander", "correct-pw").has_value());  // other users unaffected

    now += UserManager::LOCKOUT_SECONDS - 1;
    CHECK(um.isLockedOut("lockme"));
    now += 2;
    CHECK(!um.isLockedOut("lockme"));
    CHECK(um.verifyLogin("lockme", "correct-pw").has_value());

    for (int i = 0; i < UserManager::MAX_FAILED_LOGINS; ++i)  // unknown names throttle the same way
        um.verifyLogin("ghost", "whatever");
    CHECK(um.isLockedOut("ghost"));

    SessionManager sm;
    AuthHandler h(um, sm);
    for (int i = 0; i < UserManager::MAX_FAILED_LOGINS; ++i)
        h.handle(Events::AUTH_LOGIN, "{\"username\":\"bystander\",\"password\":\"nope-nope\"}");
    auto r =
        h.handle(Events::AUTH_LOGIN, "{\"username\":\"bystander\",\"password\":\"correct-pw\"}");
    CHECK(r.status == StatusCode::UNAUTHORIZED && r.body.find("too many") != std::string::npos);
}

static void testRename(const fs::path& dir) {
    UserManager um((dir / "ren_users.json").string());
    uint32_t admin = 0, bob = 0, carol = 0;
    um.registerUser("admin", "password1", "", &admin);
    um.registerUser("bob", "password1", "", &bob);
    um.registerUser("carol", "password1", "", &carol);
    const std::string db = (dir / "ren_perms.json").string();
    {
        PermissionEngine pe(db, um);
        for (const char* p :
             {"/bob/dir", "/bob/dir/f", "/bob/dir/sub/g", "/bob/dir-x", "/bob/taken"})
            CHECK(pe.registerPath(p, bob) == StatusCode::CREATED);
        pe.setSharingLevel(bob, "/bob/dir/f", SharingLevel::PUBLIC);

        CHECK(pe.renamePath(carol, "/bob/dir", "/bob/moved") == StatusCode::FORBIDDEN);
        CHECK(pe.renamePath(bob, "/nope", "/bob/moved") == StatusCode::NOT_FOUND);
        CHECK(pe.renamePath(bob, "/bob/dir", "/bob/dir") == StatusCode::BAD_REQUEST);
        CHECK(pe.renamePath(bob, "/bob/dir", "/bob/dir/inside") == StatusCode::BAD_REQUEST);
        CHECK(pe.renamePath(bob, "/bob/dir", "/bob/taken") == StatusCode::CONFLICT);
        CHECK(pe.get("/bob/dir/f").has_value() && !pe.get("/bob/moved").has_value());  // untouched

        CHECK(pe.renamePath(bob, "/bob/dir", "/bob/moved") == StatusCode::OK);
        CHECK(!pe.get("/bob/dir").has_value() && !pe.get("/bob/dir/f").has_value());
        CHECK(pe.get("/bob/moved")->owner_id == bob);
        CHECK(pe.get("/bob/moved/f")->sharing_level == SharingLevel::PUBLIC);  // metadata travels
        CHECK(pe.get("/bob/moved/sub/g").has_value());
        CHECK(pe.get("/bob/dir-x").has_value());  // same text prefix, not a child: not moved
        CHECK(pe.canAccess(carol, "/bob/moved/f", AccessType::READ));
        CHECK(!pe.canAccess(carol, "/bob/dir/f", AccessType::READ));  // old path is gone
        CHECK(pe.renamePath(admin, "/bob/moved/f", "/bob/moved/f2") == StatusCode::OK);
    }
    PermissionEngine again(db, um);
    CHECK(again.get("/bob/moved/f2").has_value() && !again.get("/bob/moved/f").has_value());
    CHECK(again.get("/bob/moved/sub/g").has_value());
}

// NFR-03: many clients at once. Run under ThreadSanitizer to catch data races.
static void testConcurrency(const fs::path& dir) {
    const std::string usersDb = (dir / "conc_users.json").string();
    constexpr int kThreads = 8, kPerThread = 5;
    {
        UserManager um(usersDb);
        SessionManager sm;
        PermissionEngine pe((dir / "conc_perms.json").string(), um);
        AuthHandler h(um, sm, &pe);

        std::atomic<int> sharedCreated{0}, created{0}, loggedIn{0}, homeOk{0};
        std::vector<std::thread> threads;
        for (int t = 0; t < kThreads; ++t) {
            threads.emplace_back([&, t] {
                if (h.handle(Events::AUTH_REGISTER,
                             "{\"username\":\"shared_name\",\"password\":\"password1\"}")
                        .status == StatusCode::CREATED)
                    ++sharedCreated;  // every thread races for the same name
                for (int i = 0; i < kPerThread; ++i) {
                    std::string name = "u" + std::to_string(t) + "_" + std::to_string(i);
                    std::string body = "{\"username\":\"" + name + "\",\"password\":\"password1\"}";
                    if (h.handle(Events::AUTH_REGISTER, body).status == StatusCode::CREATED)
                        ++created;
                    auto r = h.handle(Events::AUTH_LOGIN, body);
                    if (r.status == StatusCode::OK && sm.validate(tokenOf(r.body)))
                        ++loggedIn;
                    auto u = um.findById(um.verifyLogin(name, "password1").value_or(User{}).id);
                    if (u && pe.canAccess(u->id, "/home/" + name, AccessType::READ))
                        ++homeOk;
                }
            });
        }
        for (auto& th : threads)
            th.join();

        const int total = kThreads * kPerThread;
        CHECK(sharedCreated == 1);  // exactly one winner
        CHECK(created == total && loggedIn == total && homeOk == total);
        for (uint32_t id = 1; id <= static_cast<uint32_t>(total) + 1; ++id)
            CHECK(um.findById(id).has_value());  // ids are dense and unique
        CHECK(!um.findById(total + 2).has_value());
    }
    UserManager reloaded(usersDb);  // the file written under contention is intact
    CHECK(reloaded.findById(kThreads * kPerThread + 1).has_value());
}

int main() {
    fs::path dir = fs::temp_directory_path() / ("fsp_auth_test_" + randomHex(4));
    fs::create_directories(dir);

    testSha256();
    testUsers(dir);
    testSessions();
    testPermissions(dir);
    testHandler(dir);
    testRandom();
    testLockout(dir);
    testRename(dir);
    testConcurrency(dir);

    std::error_code ec;
    fs::remove_all(dir, ec);
    std::printf("%d checks, %d failed\n", g_checks, g_fail);
    return g_fail == 0 ? 0 : 1;
}
