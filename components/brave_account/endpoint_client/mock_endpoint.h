/* Copyright (c) 2026 The Brave Authors. All rights reserved.
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at https://mozilla.org/MPL/2.0/. */

#ifndef BRAVE_COMPONENTS_BRAVE_ACCOUNT_ENDPOINT_CLIENT_MOCK_ENDPOINT_H_
#define BRAVE_COMPONENTS_BRAVE_ACCOUNT_ENDPOINT_CLIENT_MOCK_ENDPOINT_H_

#include <variant>

#include "base/json/json_reader.h"
#include "base/json/json_writer.h"
#include "base/test/bind.h"
#include "brave/components/brave_account/endpoint_client/is_endpoint.h"
#include "net/test/embedded_test_server/embedded_test_server.h"
#include "net/test/embedded_test_server/http_request.h"
#include "net/test/embedded_test_server/http_response.h"

namespace brave_account::endpoint_client::test {

// The test helper class that creates a mock HTTP server
// handler for a specific API endpoint. It allows you to simulate different
// response scenarios (success, client error, HTTP error) for browser tests by
// intercepting requests to a particular endpoint URL and returning controlled
// responses.
//
// Usage:
//
// There are two signnatures for Will* method, with a lambda fucntion and
// RepeatingCallback.
//
// Success path:
// MockEndpoint<CreateUserEndpoint> endpoint_test(test_server);
// endpoint_test.WillSuccess([](CreateUserEndpoint::Request::DataType request) {
//   CreateUserEndpoint::Response::SuccessBody response;
//   response.user_id = 123;
//   response.created_at = base::Time::Now();
//   return response;
// });
//
// Error path:
// MockEndpoint<UpdateUserEndpoint> endpoint_test(test_server);
// endpoint_test.WillFail([](UpdateUserEndpoint::Request::DataType request) {
//   UpdateUserEndpoint::Response::ErrorBody error;
//   error.code = "INVALID_FIELD";
//   error.message = "Email format is invalid";
//   return error;
// });
//
// Http error path:
// MockEndpoint<DeleteUserEndpoint> endpoint_test(test_server);
//
// // Return custom HTTP status code
// endpoint_test.WillHttpFail([](DeleteUserEndpoint::Request::DataType request)
// {
//   // Could check request data to determine which error to return
//   if (request.user_id == 0) {
//     return net::HTTP_BAD_REQUEST;
//   }
//   return net::HTTP_INTERNAL_SERVER_ERROR;
// });
//
// // Or use default HTTP 502 from constructor
// MockEndpoint<DeleteUserEndpoint> endpoint_test{test_server};
//
// In case of default constructing, use `AddHandler` to specify the test server.

template <IsEndpoint Endpoint>
class MockEndpoint {
 public:
  using RequestDataType = typename Endpoint::Request::DataType;

  using Success =
      base::RepeatingCallback<typename Endpoint::Response::SuccessBody(
          RequestDataType)>;

  using Fail = base::RepeatingCallback<typename Endpoint::Response::ErrorBody(
      RequestDataType)>;

  using HttpFail =
      base::RepeatingCallback<net::HttpStatusCode(RequestDataType)>;

  MockEndpoint() {
    WillHttpFail([](RequestDataType) -> net::HttpStatusCode {
      return net::HTTP_BAD_GATEWAY;
    });
  }

  explicit MockEndpoint(net::EmbeddedTestServer& test_server) : MockEndpoint() {
    AddHandler(test_server);
  }

  void AddHandler(net::EmbeddedTestServer& test_server) {
    auto call = [this](const net::test_server::HttpRequest& request)
        -> std::unique_ptr<net::test_server::HttpResponse> {
      return HttpHandler(request);
    };
    test_server.RegisterRequestHandler(
        base::BindLambdaForTesting(std::move(call)));
  }

  template <typename F>
  void WillSuccess(F&& on_call) {
    auto call = [&](RequestDataType data) ->
        typename Endpoint::Response::SuccessBody {
          return std::invoke(std::forward<F>(on_call), std::move(data));
        };
    WillSuccess(base::BindLambdaForTesting(std::move(call)));
  }

  void WillSuccess(Success on_call) { on_call_handler_ = std::move(on_call); }

  template <typename F>
  void WillFail(F&& on_call) {
    auto call = [&](RequestDataType data) ->
        typename Endpoint::Response::ErrorBody {
          return std::invoke(std::forward<F>(on_call), std::move(data));
        };
    WillFail(base::BindLambdaForTesting(std::move(call)));
  }

  void WillFail(Fail on_call) { on_call_handler_ = std::move(on_call); }

  template <typename F>
  void WillHttpFail(F&& on_call) {
    auto call = [&](RequestDataType data) -> net::HttpStatusCode {
      return std::invoke(std::forward<F>(on_call), std::move(data));
    };
    WillHttpFail(base::BindLambdaForTesting(std::move(call)));
  }

  void WillHttpFail(HttpFail on_call) { on_call_handler_ = std::move(on_call); }

 private:
  template <class... Ts>
  struct Overloaded : Ts... {
    using Ts::operator()...;
  };

  std::unique_ptr<net::test_server::HttpResponse> HttpHandler(
      const net::test_server::HttpRequest& request) {
    if (request.GetURL().path() != Endpoint::URL().path() ||
        request.method_string != Endpoint::Request::Method()) {
      return nullptr;
    }

    auto response = std::make_unique<net::test_server::BasicHttpResponse>();

    auto endpoint_request =
        Endpoint::Request::FromValue(
            base::JSONReader::Read(request.content, base::JSON_PARSE_RFC)
                .value_or(base::Value(base::DictValue())))
            .value();

    std::visit(
        Overloaded{
            [&](Success handler) {
              auto success_body = handler.Run(std::move(endpoint_request));
              response->set_content_type("application/json");
              response->set_code(net::HTTP_OK);
              response->set_content(
                  base::WriteJson(success_body.ToValue()).value());
            },
            [&](Fail handler) {
              auto error_body = handler.Run(std::move(endpoint_request));
              response->set_content_type("application/json");
              response->set_code(net::HTTP_BAD_REQUEST);
              response->set_content(
                  base::WriteJson(error_body.ToValue()).value());
            },
            [&](HttpFail handler) {
              response->set_code(handler.Run(std::move(endpoint_request)));
            }},
        on_call_handler_);

    return response;
  }

  std::variant<Success, Fail, HttpFail> on_call_handler_;
};

}  // namespace brave_account::endpoint_client::test

#endif  // BRAVE_COMPONENTS_BRAVE_ACCOUNT_ENDPOINT_CLIENT_MOCK_ENDPOINT_H_
