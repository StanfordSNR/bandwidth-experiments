#include <set>

#include "nat/peer.hh"
#include "net/socket.hh"
#include "util/eventloop.hh"
#include "util/split.hh"
#include "util/timerfd.hh"

using namespace std;
using namespace std::chrono;

ofstream fout { "/tmp/out" };

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

  size_t bytes_transferred { 0 };

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
  if ( argc < 5 ) {
    cerr << "Usage: lambdafunc <master_ip> <master_port> <thread_id> <duration> <block_dim> "
         << "<active-worker>..." << endl;
    return EXIT_FAILURE;
  }

  EventLoop loop;

  const string master_ip { argv[1] };
  const uint16_t master_port = static_cast<uint16_t>( stoul( argv[2] ) );
  const uint32_t thread_id = static_cast<uint32_t>( stoul( argv[3] ) );
  const uint32_t experiment_duration = static_cast<uint32_t>( stoul( argv[4] ) );
  const uint32_t block_dim = static_cast<uint32_t>( stoul( argv[5] ) );

  list<Worker> peers;
  size_t max_peer_id = 0;

  for ( auto& [peer_id, peer_ip] : get_peer_addresses( thread_id, master_ip, master_port, block_dim, fout ) ) {
    peers.emplace(
      peers.end(), peer_id, Address { peer_ip, static_cast<uint16_t>( 14000 + peer_id ) }, WorkerType::Send );

    peers.emplace(
      peers.end(), peer_id, Address { peer_ip, static_cast<uint16_t>( 18000 + peer_id ) }, WorkerType::Recv );

    max_peer_id = max( max_peer_id, peer_id );
  }

  vector<bool> send_workers( max_peer_id + 1 );
  vector<bool> recv_workers( max_peer_id + 1 );

  for ( int i = 6; i < argc; i++ ) {
    if ( argv[i][0] == 'x' ) {
      send_workers[atoi( &argv[i][1] )] = true;
    } else {
      recv_workers[atoi( argv[i] )] = true;
    }
  }

  string send_buffer = generate_random_buffer( 1 * 1024 * 1024 );
  string read_buffer( 1 * 1024 * 1024, '\0' );

  size_t bytes_sent = 0;
  size_t bytes_recv = 0;

  const auto start = steady_clock::now();

  for ( auto& peer : peers ) {
    peer.socket.bind( { "0", static_cast<uint16_t>( ( peer.type == WorkerType::Send ? 18000 : 14000 ) + thread_id ) } );

    peer.socket.set_blocking( false );
    peer.socket.connect( peer.addr );

    loop.add_rule(
      "peer"s + to_string( peer.thread_id ),
      peer.socket,
      [&] { bytes_recv += peer.socket.read( { read_buffer } ); },
      [&] { return peer.type == WorkerType::Send and recv_workers[thread_id] and send_workers[peer.thread_id]; },
      [&] { bytes_sent += peer.socket.write( send_buffer ); },
      [&] { return peer.type == WorkerType::Recv and send_workers[thread_id] and recv_workers[peer.thread_id]; },
      [&] { fout << "peer died " << peer.thread_id << endl; } );
  }

  bool terminated = false;

  TimerFD logging_timer { seconds { 1 } };

  loop.add_rule(
    "log",
    Direction::In,
    logging_timer,
    [&] {
      logging_timer.read_event();
      fout << thread_id << "," << duration_cast<seconds>( steady_clock::now() - start ).count() << "," << bytes_sent
           << "," << bytes_recv << endl;
    },
    [] { return true; } );

  TimerFD termination_timer { seconds { experiment_duration } };

  loop.add_rule(
    "termination", Direction::In, termination_timer, [&] { peers.clear(); }, [&] { return not terminated; } );

  loop.set_fd_failure_callback( [&] {
    fout << "socket error occurred" << endl;
    terminated = true;
  } );

  while ( not terminated and loop.wait_next_event( -1 ) != EventLoop::Result::Exit )
    ;

  const auto end = steady_clock::now();

  fout << "time=" << duration_cast<milliseconds>( end - start ).count() << endl
       << "total_bytes_sent=" << bytes_sent << endl
       << "total_bytes_recv=" << bytes_recv << endl;

  return EXIT_SUCCESS;
}
