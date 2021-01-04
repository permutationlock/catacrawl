#include <thread>
#include <chrono>
#include <functional>
#include <spdlog/spdlog.h>

#define DISABLE_PICOJSON
#include <jwt-cpp/jwt.h>

#include <jwt_game_server/matchmaking_server.hpp>
#include <json_traits/nlohmann_traits.hpp>
#include <websocketpp_configs/asio_no_logs.hpp>

#include "../tic_tac_toe_game.hpp"

using namespace std::chrono_literals;
using claim = jwt::basic_claim<nlohmann_traits>;
using player_id = tic_tac_toe_matchmaking_data::player_id;

using ttt_server = jwt_game_server::matchmaking_server<
    tic_tac_toe_matchmaking_data,
    jwt::default_clock, nlohmann_traits,
    asio_no_logs
  >;

int main() {
  // log level
  spdlog::set_level(spdlog::level::trace);

  const std::string secret = "secret";

  // create a jwt verifier
  jwt::verifier<jwt::default_clock, nlohmann_traits> 
    verifier(jwt::default_clock{});
  verifier.allow_algorithm(jwt::algorithm::hs256(secret))
    .with_issuer("tic_tac_toe_auth");

  // create a function to sign game tokens
  auto sign_game = [=](player_id id, const json& data){ 
      return jwt::create<nlohmann_traits>()
        .set_issuer("tic_tac_toe_matchmaker")
        .set_payload_claim("id", claim(id))
        .set_payload_claim("data", claim(data))
        .sign(jwt::algorithm::hs256{secret});
    };

  // create our main server to manage player connection and matchmaking
  ttt_server mms{verifier, sign_game, 100ms};

  std::thread mms_server_thr{bind(&ttt_server::run, &mms, 9091, true)};

  while(!mms.is_running()) {
    std::this_thread::sleep_for(10ms);
  }

  std::thread msg_process_thr{bind(&ttt_server::process_messages, &mms)};

  std::thread match_thr{bind(&ttt_server::match_players, &mms)};

  mms_server_thr.join();
  msg_process_thr.join();
  match_thr.join();
}