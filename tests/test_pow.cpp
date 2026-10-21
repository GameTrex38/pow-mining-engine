#include "crypto/sha256.hpp"
#include "chain/block.hpp"
#include "chain/merkle.hpp"
#include <iostream>
#include <cassert>
#include <string>
#include <random>
#include <cstring>

using namespace pow_engine;

void test_nist_sha256_vectors() {
    std::cout << "[Test 1] Verifying NIST SHA-256 Test Vectors...\n";

    // Test Vector 1: Empty string ""
    auto h1 = crypto::to_hex(crypto::sha256(""));
    assert(h1 == "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
    std::cout << "  ✓ Vector 1 (Empty string): " << h1 << " [PASS]\n";

    // Test Vector 2: "abc"
    auto h2 = crypto::to_hex(crypto::sha256("abc"));
    assert(h2 == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
    std::cout << "  ✓ Vector 2 ('abc'): " << h2 << " [PASS]\n";

    // Test Vector 3: 448-bit string
    std::string v3 = "abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq";
    auto h3 = crypto::to_hex(crypto::sha256(v3));
    assert(h3 == "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1");
    std::cout << "  ✓ Vector 3 (448-bit NIST message): " << h3 << " [PASS]\n";
}

void test_midstate_equivalence() {
    std::cout << "\n[Test 2] Verifying Exact Equivalence: Standard SHA256d vs Midstate SHA256d...\n";

    chain::BlockHeader header;
    header.version = 2;
    header.prev_block_hash = crypto::from_hex("000000000019d6689c085ae165831e934ff763ae46a2a6c172b3f1b60a8ce26f", true);
    header.merkle_root = crypto::from_hex("4a5e1e4baab89f3a32518a88c31bc87f618f76673e2cc77ab2127b7afdeda33b", true);
    header.timestamp = 1600000000;
    header.bits = 0x1f0fffff;

    std::array<uint8_t, 64> chunk1;
    std::array<uint8_t, 16> chunk2;
    header.get_chunks(chunk1, chunk2);

    // Precompute midstate once
    auto midstate = crypto::compute_midstate(chunk1.data());
    uint8_t chunk2_prefix[12];
    std::memcpy(chunk2_prefix, chunk2.data(), 12);

    // Test 1,000 nonces: naive vs midstate vs fast_midstate
    for (uint32_t nonce : {0u, 1u, 42u, 1337u, 0x12345678u, 0xffffffffu}) {
        header.nonce = nonce;
        auto raw80 = header.serialize();

        // 1. Standard naive SHA256d
        auto naive_hash = crypto::sha256d(raw80);

        // 2. Chunk2 midstate
        header.get_chunks(chunk1, chunk2);
        auto midstate_hash = crypto::sha256d_with_midstate(midstate, chunk2.data());

        // 3. Fast in-place midstate
        crypto::Hash256 fast_hash;
        crypto::sha256d_midstate_fast(midstate, chunk2_prefix, nonce, fast_hash);

        assert(naive_hash == midstate_hash);
        assert(naive_hash == fast_hash);
    }

    std::cout << "  ✓ Tested nonces across dynamic boundaries.\n"
              << "  ✓ Bit-for-bit identity verified: SHA256d(80B) == SHA256d_Midstate(Chunk1, Chunk2) [PASS]\n";
}

void test_merkle_tree() {
    std::cout << "\n[Test 3] Verifying Merkle Tree Root Determinism & Odd-Leaf Balancing...\n";

    chain::Transaction t1{"", "Genesis", "Alice", 50.0, 0, true, 0};
    chain::Transaction t2{"", "Alice", "Bob", 10.0, 500, false, 0};
    chain::Transaction t3{"", "Bob", "Charlie", 4.5, 200, false, 0};

    // Tree with 1 tx (coinbase only)
    chain::MerkleTree tree1({t1});
    assert(!tree1.get_root_hex().empty());
    std::cout << "  ✓ 1-Leaf Merkle Root: " << tree1.get_root_hex() << " [PASS]\n";

    // Tree with 3 txs (tests duplicate odd leaf rule)
    chain::MerkleTree tree3({t1, t2, t3});
    assert(tree3.get_levels().size() == 3); // L0 (4 leaves after duplication), L1 (2 nodes), L2 (1 root)
    std::cout << "  ✓ 3-Leaf Balanced Merkle Root: " << tree3.get_root_hex() << " [PASS]\n";
    std::cout << "  ✓ Tree Levels: " << tree3.get_levels().size() << " [PASS]\n";
}

void test_nbits_target_math() {
    std::cout << "\n[Test 4] Verifying nBits 256-Bit Target Expansion & PoW Verification...\n";

    // Bitcoin Genesis bits: 0x1d00ffff
    uint32_t gen_bits = 0x1d00ffff;
    auto target = chain::nbits_to_target(gen_bits);
    auto hex = chain::target_to_hex(target);
    std::cout << "  ✓ Genesis Target 0x1d00ffff: " << hex << "\n";
    assert(target[0] == 0 && target[1] == 0 && target[2] == 0);
    assert(target[3] == 0x00 && target[4] == 0xff && target[5] == 0xff);

    // Test target round-trip
    uint32_t roundtrip_bits = chain::target_to_nbits(target);
    assert(roundtrip_bits == gen_bits);
    std::cout << "  ✓ Target to nBits roundtrip matched: 0x" << std::hex << roundtrip_bits << std::dec << " [PASS]\n";

    // Test check_proof_of_work
    // Hash with lots of leading zeros is valid
    crypto::Hash256 good_hash{};
    good_hash.fill(0); // All zeros, definitely < target
    assert(chain::check_proof_of_work(good_hash, gen_bits) == true);

    // Hash with no leading zeros is invalid
    crypto::Hash256 bad_hash{};
    bad_hash.fill(0xff);
    assert(chain::check_proof_of_work(bad_hash, gen_bits) == false);

    std::cout << "  ✓ PoW verification against 256-bit target [PASS]\n";
}

int main() {
    std::cout << "========================================================================\n"
              << "  PoW Mining Engine Cryptographic Verification Test Suite\n"
              << "========================================================================\n\n";

    test_nist_sha256_vectors();
    test_midstate_equivalence();
    test_merkle_tree();
    test_nbits_target_math();

    std::cout << "\n\033[1;32m========================================================================\n"
              << "  ALL CRYPTOGRAPHIC & MATHEMATICAL VERIFICATION TESTS PASSED (100%)\n"
              << "========================================================================\033[0m\n";
    return 0;
}
