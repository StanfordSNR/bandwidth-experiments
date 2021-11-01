#include <iostream>

#include "net/http_client.hh"
#include "net/lambda.hh"

using namespace std;

int main( int argc, char* argv[] )
{
  if ( argc != 2 ) {
    cerr << "Usage: run-experiment WORKER-COUNT" << endl;
    return EXIT_FAILURE;
  }

  const size_t worker_count = stoull( argv[1] );
  const string region = "us-west-1";
  const string function_name = "bandwidth-experiments";

  EventLoop loop;

  // let's invoke the lambdas
  for ( size_t i = 0; i < worker_count; i++ ) {
    HTTPRequest invocation_request = LambdaInvocationRequest( {},
                                                              "us-west-1",
                                                              "function-name",
                                                              "",
                                                              LambdaInvocationRequest::InvocationType::REQUEST_RESPONSE,
                                                              LambdaInvocationRequest::LogType::NONE )
                                       .to_http_request();
  }

  return EXIT_SUCCESS;
}
