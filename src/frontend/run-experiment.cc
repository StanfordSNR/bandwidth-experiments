#include <iostream>

#include "net/address.hh"
#include "net/http_client.hh"
#include "net/lambda.hh"
#include "net/session.hh"
#include "util/json.hh"

using namespace std;

int main( int argc, char* argv[] )
{
  if ( argc != 4 ) {
    cerr << "Usage: run-experiment WORKER-COUNT DURATION TOPOLOGY" << endl;
    return EXIT_FAILURE;
  }

  const size_t worker_count = stoull( argv[1] );
  const size_t duration = stoull( argv[2] );
  const string topology { argv[3] };
  
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

  // let's invoke the lambdas
  for ( size_t i = 0; i < worker_count; i++ ) {
    HTTPRequest invocation_request = LambdaInvocationRequest( {},
                                                              region,
                                                              function_name,
                                                              "{}",
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

        cout << j["stdout"].get<string>();
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
