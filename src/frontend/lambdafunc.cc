#include <iostream>
#include <random>
#include <vector>

#include "nat/peer.hh"
#include "net/socket.hh"
#include "util/eventloop.hh"
#include "util/split.hh"
#include "util/timerfd.hh"

using namespace std;
using namespace std::chrono;

enum class WorkerType
{
  Send,
  Recv
};

struct Worker
{
  size_t thread_id;
  Address addr;
  TCPSocket socket {};
  WorkerType type;

  Worker( const size_t thread_id_, const Address& addr_, const WorkerType type_ )
    : thread_id( thread_id_ )
    , addr( addr_ )
    , type( type_ )
  {
    socket.set_reuseaddr();
    socket.set_blocking( false );
  }
};

string generate_random_buffer( const size_t len )
{
  srand( time( nullptr ) );
  string res( len, '\0' );
  for ( size_t i = 0; i < len; i++ ) {
    res[i] = static_cast<char>( rand() % 256 );
  }
  return res;
}

int main( int argc, char* argv[] )
{
  if ( argc < 7 ) {
    cerr << "Usage: lambdafunc <master_ip> <master_port> <worker_count> <thread_id> <duration> <block_dim> "
         << "<active-worker>..." << endl;
    return EXIT_FAILURE;
  }

  EventLoop loop;

  const string master_ip { argv[1] };
  const uint16_t master_port = static_cast<uint16_t>( stoul( argv[2] ) );
  const uint32_t worker_count = static_cast<uint32_t>( stoul( argv[3] ) );
  const uint32_t thread_id = static_cast<uint32_t>( stoul( argv[4] ) );
  const uint32_t experiment_duration = static_cast<uint32_t>( stoul( argv[5] ) );
  const uint32_t block_dim = static_cast<uint32_t>( stoul( argv[6] ) );

  vector<shared_ptr<Worker>> peers;

  for ( auto& [peer_id, peer_ip] : get_peer_addresses( thread_id, master_ip, master_port, block_dim ) ) {
    peers.emplace_back(
      make_shared<Worker>( peer_id, Address { peer_ip, static_cast<uint16_t>( 14000 + peer_id ) }, WorkerType::Send ) );

    peers.emplace_back(
      make_shared<Worker>( peer_id, Address { peer_ip, static_cast<uint16_t>( 18000 + peer_id ) }, WorkerType::Recv ) );
  }

  vector<bool> send_workers( worker_count );
  vector<bool> recv_workers( worker_count );

  for ( int i = 7; i < argc; i++ ) {
    if ( argv[i][0] == 'x' ) {
      send_workers[atoi( &argv[i][1] )] = true;
    } else {
      recv_workers[atoi( argv[i] )] = true;
    }
  }

  string send_buffer = generate_random_buffer( 128 * 1024 ); // 1 MiB
  string read_buffer( 1 * 1024 * 1024, '\0' );

  size_t bytes_sent = 0;
  size_t bytes_recv = 0;

  auto peer_category = loop.add_category( "peer" );
  const auto start = steady_clock::now();

  vector<size_t> all_peer_idxs( peers.size() );
  iota( all_peer_idxs.begin(), all_peer_idxs.end(), 0 );
  shuffle( all_peer_idxs.begin(), all_peer_idxs.end(), mt19937 { random_device {}() } );

  for ( const auto i : all_peer_idxs ) {
    peers[i]->socket.bind(
      { "0", static_cast<uint16_t>( ( peers[i]->type == WorkerType::Send ? 18000 : 14000 ) + thread_id ) } );

    peers[i]->socket.set_blocking( false );
    peers[i]->socket.connect( peers[i]->addr );

    loop.add_rule(
      peer_category,
      peers[i]->socket,
      [&, &p = *peers[i]] { bytes_recv += p.socket.read( { read_buffer } ); },
      [&, &p = *peers[i]] {
        return p.type == WorkerType::Send and recv_workers[thread_id] and send_workers[p.thread_id];
      },
      [&, &p = *peers[i]] { bytes_sent += p.socket.write( send_buffer ); },
      [&, &p = *peers[i]] {
        return p.type == WorkerType::Recv and send_workers[thread_id] and recv_workers[p.thread_id];
      },
      [&, &p = *peers[i]] { cerr << "peer died " << p.thread_id << endl; } );
  }

  bool terminated = false;

  TimerFD logging_timer { seconds { 1 } };

  loop.add_rule(
    "log",
    Direction::In,
    logging_timer,
    [&] {
      logging_timer.read_event();
      cout << thread_id << "," << duration_cast<seconds>( steady_clock::now() - start ).count() << "," << bytes_sent
           << "," << bytes_recv << endl;
    },
    [] { return true; } );

  TimerFD termination_timer { seconds { experiment_duration } };

  loop.add_rule(
    "termination",
    Direction::In,
    termination_timer,
    [&] {
      terminated = true;
      peers.clear();
    },
    [&] { return not terminated; } );

  loop.set_fd_failure_callback( [&] {
    cerr << "socket error occurred" << endl;
    terminated = true;
  } );

  while ( not terminated and loop.wait_next_event( -1 ) != EventLoop::Result::Exit )
    ;

  const auto end = steady_clock::now();

  cerr << "time=" << duration_cast<milliseconds>( end - start ).count() << endl
       << "total_bytes_sent=" << bytes_sent << endl
       << "total_bytes_recv=" << bytes_recv << endl;

  return EXIT_SUCCESS;
}
