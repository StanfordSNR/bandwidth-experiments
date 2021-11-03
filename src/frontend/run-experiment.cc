#include <iostream>
#include <random>

#include "net/address.hh"
#include "net/http_client.hh"
#include "net/lambda.hh"
#include "net/session.hh"
#include "util/json.hh"

using namespace std;

int main( int argc, char* argv[] )
{
  if ( argc != 6 ) {
    cerr << "Usage: run-experiment <master_ip> <master_port> <worker_count> <duration> <topology>" << endl;
    return EXIT_FAILURE;
  }

  // "Usage: lambdafunc <master_ip> <master_port> <worker_count> <thread_id> <duration> <block_dim> "

  const string master_ip { argv[1] };
  const string master_port { argv[2] };
  const size_t worker_count = stoull( argv[3] );
  const size_t duration = stoull( argv[4] );
  const string topology { argv[5] };

  vector<string> lambda_args;
  lambda_args.push_back( master_ip );
  lambda_args.push_back( master_port );
  lambda_args.push_back( to_string( worker_count ) );
  lambda_args.push_back( "" ); // worker_id placeholder
  lambda_args.push_back( to_string( duration ) );
  lambda_args.push_back( "1" ); // block_dim

  if ( topology == "a2a" ) {
    for ( size_t i = 0; i < worker_count; i++ ) {
      lambda_args.push_back( "x" + to_string( i ) );
      lambda_args.push_back( to_string( i ) );
    }
  } else if ( topology == "group5" ) {
    for ( size_t i = 0; i < worker_count; i++ ) {
      lambda_args.push_back( "x" + to_string( i ) );
      lambda_args.push_back( to_string( i ) );
    }

    lambda_args[5] = to_string( worker_count / 5 );
  } else {
    throw runtime_error( "unknown topology: " + topology );
  }

  const string region = "us-west-1";
  const string function_name = "tempf";

  EventLoop loop;
  SSLContext ssl_context;

  HTTPClient<SSLSession>::RuleCategories rule_categories {
    loop.add_category( "session" ),
    loop.add_category( "endpoint read" ),
    loop.add_category( "endpoint write" ),
    loop.add_category( "response" ),
  };

  list<HTTPClient<SSLSession>> http_clients;
  list<decltype( http_clients )::iterator> finished_clients;

  Address aws_address { LambdaInvocationRequest::endpoint( region ), "https" };

  size_t response_count = 0;
  bool terminated = false;

  vector<size_t> invocation_order( worker_count );
  iota( invocation_order.begin(), invocation_order.end(), 0 );
  shuffle( invocation_order.begin(), invocation_order.end(), mt19937 { random_device {}() } );

  // let's invoke the lambdas
  for ( const auto i : invocation_order ) {
    lambda_args[3] = to_string( i );

    nlohmann::json event_payload = { { "args", lambda_args } };

    HTTPRequest invocation_request = LambdaInvocationRequest( {},
                                                              region,
                                                              function_name,
                                                              event_payload.dump(),
                                                              LambdaInvocationRequest::InvocationType::REQUEST_RESPONSE,
                                                              LambdaInvocationRequest::LogType::NONE )
                                       .to_http_request();

    TCPSocket socket;
    socket.set_blocking( false );
    socket.connect( aws_address );
    http_clients.emplace_back( SSLSession { ssl_context.make_SSL_handle(), move( socket ) } );

    auto it = prev( http_clients.end() );

    it->install_rules(
      loop,
      rule_categories,
      [&, it, i]( HTTPResponse&& response ) {
        nlohmann::json j = nlohmann::json::parse( response.body() );

        cerr << "==== worker " << i << " (stdout) ====" << endl;
        cout << j["stdout"].get<string>();
        cerr << "==== worker " << i << " (stderr) ====" << endl;
        cerr << j["stderr"].get<string>();

        if ( j["retcode"].get<int>() ) {
          throw runtime_error( "worker failed: " + to_string( i ) );
        }

        it->uninstall_rules();
        finished_clients.push_back( it );
        response_count++;
      },
      [&, it, i] { finished_clients.push_back( it ); },
      make_optional( [] { throw runtime_error( "error occurred" ); } ) );

    it->push_request( move( invocation_request ) );
  }

  // csv output header
  cout << "worker,peer,time,bytes_sent,bytes_received" << endl;

  loop.add_rule(
    "done", [&terminated] { terminated = true; }, [&] { return not terminated and response_count == worker_count; } );

  while ( not terminated && loop.wait_next_event( -1 ) != EventLoop::Result::Exit ) {
    for ( auto& fit : finished_clients ) {
      http_clients.erase( fit );
    }

    finished_clients.clear();
  }

  return EXIT_SUCCESS;
}
