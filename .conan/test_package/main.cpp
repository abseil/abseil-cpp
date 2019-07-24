#include <string>
#include <utility>
#include <iostream>
#include <vector>

#include "absl/strings/str_cat.h"
#include "absl/strings/str_split.h"
#include "absl/container/flat_hash_map.h"
#include "absl/container/flat_hash_set.h"
#include "absl/numeric/int128.h"
#include "absl/time/time.h"

int main(int argc, const char** argv) {
    absl::flat_hash_set<std::string> set1;
    absl::flat_hash_map<int, std::string> map1;
    absl::flat_hash_set<std::string> set2 = {{"huey"}, {"dewey"}, {"louie"},};
    absl::flat_hash_map<int, std::string> map2 = {{1, "huey"}, {2, "dewey"}, {3, "louie"},};
    absl::flat_hash_set<std::string> set3(set2);
    absl::flat_hash_map<int, std::string> map3(map2);

    absl::flat_hash_set<std::string> set4;
    set4 = set3;
    absl::flat_hash_map<int, std::string> map4;
    map4 = map3;

    absl::flat_hash_set<std::string> set5(std::move(set4));
    absl::flat_hash_map<int, std::string> map5(std::move(map4));
    absl::flat_hash_set<std::string> set6;
    set6 = std::move(set5);
    absl::flat_hash_map<int, std::string> map6;
    map6 = std::move(map5);

    const absl::uint128 big = absl::Uint128Max();
	std::cout << absl::StrCat("Arg ", "foo", "\n");
    std::vector<std::string> v = absl::StrSplit("a,b,,c", ',');

    absl::Time t1 = absl::Now();
    absl::Time t2 = absl::Time();
    absl::Time t3 = absl::UnixEpoch();

    return 0;
}