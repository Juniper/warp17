# Contributing a new L7 Application implementation
WARP17 currently supports _RAW TCP_ and _HTTP 1.1_ application traffic. Even
though we are currently working on adding support for more application
implementations, external contributions are welcome.

As a future development WARP17 will offer a socket API in order to allow
applications to be easily plugged in. Until then any new application must
be directly added to the WARP17 code. As an example, a good starting point
is the _HTTP 1.1_ implementation itself.

In general, an application called `foo` should implement the following:

* `warp17-app-foo.proto` definition file in `api/`: should contain
  the application configuration definitions (for clients and servers) and
  preferably application specific statistics definitions.

  	- `warp17-app-foo.proto` should be included in `warp17-app.proto` and
		  the application the `App` structure:

	```
	message App {
	  required AppProto app_proto = 1;

	    /* Add different App configs below as optionals. */
	  optional RawClient  app_raw_client  = 2 [(warp17_union_anon) = true];
	  optional RawServer  app_raw_server  = 3 [(warp17_union_anon) = true];
	  optional HttpClient app_http_client = 4 [(warp17_union_anon) = true];
	  optional HttpServer app_http_server = 5 [(warp17_union_anon) = true];
	  optional Imix       app_imix        = 6 [(warp17_union_anon) = true];
		optional Foo		    app_foo					= 7 [(warp17_union_anon) = true];
	}
	```

    - the application specific statistics should also be added to the
    `AppStats` definition:

	```
	message AppStats {
	  /* The user will do the translation. */
	  option (warp17_xlate_tpg) = false;

	  optional RawStats  as_raw  = 1 [(warp17_union_anon) = true];
	  optional HttpStats as_http = 2 [(warp17_union_anon) = true];
	  optional ImixStats as_imix = 3 [(warp17_union_anon) = true];
		optional FooStats  as_foo  = 4 [(warp17_union_anon) = true];
	}
	```

    - a new entry for the application type should be added to the `AppProto`
      enum in `warp17-common.proto`:

	```
	enum AppProto {
	    RAW_CLIENT    = 0;
	    RAW_SERVER    = 1;
	    HTTP_CLIENT   = 2;
	    HTTP_SERVER   = 3;
	    IMIX          = 4;
			FOO           = 5;
	    APP_PROTO_MAX = 6;
	}
	```

    - the new protocol buffer file (`warp17-app-foo.proto`) should also
      be added to `api/Makefile.api`:

	```
	PROTO-SRCS += warp17-app-raw.proto
	PROTO-SRCS += warp17-app-http.proto
	PROTO-SRCS += warp17-app-foo.proto
	```

    - the file `Makefile.dpdk` should also be updated to include the new
      application implementation:

	```
	SRCS-y += tpg_test_app.c
	SRCS-y += tpg_test_http_1_1_app.c
	SRCS-y += tpg_test_raw_app.c
	SRCS-y += tpg_test_foo_app.c
	```

    - include `warp17-app-foo.proto` in `tcp_generator.h`:

	```
	#include "warp17-app-raw.proto.xlate.h"
	#include "warp17-app-http.proto.xlate.h"
	#include "warp17-app-foo.proto.xlate.h"
	```

* RPC WARP17 to protobuf translation code:

	- a new case entry in `tpg_xlate_tpg_union_App` where the application
	  translation function should be called:

	```
  case APP_PROTO__HTTP_SERVER:
      out->app_http_server =
          rte_zmalloc("TPG_RPC_GEN", sizeof(*out->app_http_server), 0);
      if (!out->app_http_server)
          return -ENOMEM;

      tpg_xlate_tpg_HttpServer(&in->app_http_server, out->app_http_server);
      break;
	case APP_PROTO__FOO:
	    out->app_foo = rte_zmalloc("TPG_RPC_GEN", sizeof(*out->app_foo), 0);
	    if (!out->app_foo)
	        return -ENOMEM;

	    tpg_xlate_tpg_Foo(&in->app_foo, out->app_foo);
	    break;
	```

	- a new case entry in `tpg_xlate_tpgTestAppStats_by_proto` when translating
	  application statistics:

	```
	case APP_PROTO__HTTP_SERVER:
      out->as_http = rte_zmalloc("TPG_RPC_GEN", sizeof(*out->as_http), 0);
      if (!out->as_http)
         return -ENOMEM;

      err = tpg_xlate_tpg_HttpStats(&in->as_http, out->as_http);
      if (err)
          return err;
      break;
	case APP_PROTO__FOO:
      out->as_foo = rte_zmalloc("TPG_RPC_GEN", sizeof(*out->as_foo), 0);
      if (!out->as_foo)
          return -ENOMEM;

      err = tpg_xlate_tpg_FooStats(&in->as_foo, out->as_foo);
      if (err)
          return err;
      break;
	```

* `appl/tpg_test_app.h` interface implementation:

	- application `foo` should be added to the `app_data_t` definition in
	  `inc/appl/tpg_test_app.h`. Type `foo_app_t` should be defined in the
	  application headers and should represent a state storage for the
	  `foo` application. The state is part of the L4 control block structures
	  (TCB/UCB).

	```
	typedef struct app_data_s {

	    [...]

	    union {
	        raw_app_t     ad_raw;
	        http_app_t    ad_http;
	        foo_app_t     ad_foo;
	        generic_app_t ad_generic;
	    };

	} app_data_t;
	```

	- `foo` must also provide callback functions corresponding to the callback
	  types defined in `inc/appl/tpg_test_app.h`. The callbacks should be
	  added to the callback arrays in `src/appl/tpg_test_app.c`. These
	  functions will be called by the test engine whenever application
	  intervention is required:

		- `app_default_cfg_cb_t`: should initialize the `foo` application
		  config to default values

		- `app_validate_cfg_cb_t`: should validate the config corresponding to
		  the `foo` application

		- `app_print_cfg_cb_t`: should display the part of the configuration
		  corresponding to the `foo` application by using the supplied printer

		- `app_add_cfg_cb_t`: will be called whenever a test case is added
		  so `foo` initialize everything needed for the test case.

		- `app_delete_cfg_cb_t`: will be called whenever a test case is deleted
		  so `foo` can cleanup anything it initialized for the test case.

		- `app_pkts_per_send_cb_t`: will be called when the test case is started
		  to determine how many packets (on average) will `foo` be sending for
		  a single data message

		- `app_init_cb_t`: will be called whenever a session is initialized
		  and should initialize the application state.

		- `app_tc_start_stop_cb_t`: `foo` should define two callbacks (for
		  test case start and stop). The application should initialize and
		  cleanup any data that is required during the test case (e.g.,
		  predefined static data headers)

		- `app_conn_up_cb_t`: will be called whenever a session has been
		  established

		- `app_conn_down_cb_t`: will be called whenever a session closed (
		  either because the underlying connection went down or because the
		  application itself decided to close the connection)

		- `app_deliver_cb_t`: will be called whenever there was data received
		  for the application to process. The received data is passed as an
		  mbuf chain. The callback should return the total number of bytes
		  that were consumed. For example, in the case of TCP these bytes will
		  be ACK-ed and removed from the receive window.

		- `app_send_cb_t`: will be called whenever the test engine can send
		  data on the current connection. The application can decide at any
		  time that it would like to start sending or stop sending data by
		  notifying the test engine through the
		  `TEST_NOTIF_APP_CLIENT/SERVER_SEND_STOP/START` notifications.
		  The `app_send_cb_t` callback should return an `mbuf` chain pointing
		  to the data it would like to send.
		  In general, freeing the `mbuf` upon sending is the job of the
		  TCP/IP stack so the application must make sure that it doesn't
		  continue using the mbuf after passing it to the test engine.
		  __NOTE: However, for some applications, in order to avoid building
		  packets every time, the implementation might prefer to reuse data
		  templates (e.g., HTTP requests can be easily prebuilt when the test
		  case is started). In such a situation the application can mark the
		  mbufs as _STATIC_ through the `DATA_SET_STATIC` call which will
		  inform the test engine that it shouldn't free the data itself. The
		  application must ensure in such a case that the data itself is never
		  freed during the execution of the test case!__

		- `app_data_sent_cb_t`: will be called to notify the application that
		  (part of) the data was sent. It might happen that not all the data
		  could be sent in one shot so the application should return `true`
		  if what was sent corresponds to a complete message

		- `app_stats_add_cb_t`: should aggregate application specific
		  statistics

		- `app_stats_print_cb_t`: should print application specific statistics
		  using the supplied printer

	- the `foo` application can request the test engine to perform
	  operations by sending the following notifications:
	  	- `TEST_NOTIF_APP_CLIENT/SERVER_SEND_START`: notifies the test engine
	  	  that the application would like to send data (when possible) on the
	  	  current connection

	  	- `TEST_NOTIF_APP_CLIENT/SERVER_SEND_STOP`: notifies the test engine
	  	  that the application has finished sending data (for now) on the
	  	  current connection

	  	- `TEST_NOTIF_APP_CLIENT/SERVER_CLOSE`: notifies the test engine that
	  	  the application would like to close the connection

* CLI: the `foo` application can define it's own CLI commands using the DPDK
  cmdline infrastructure. These can be added to a local `cli_ctx` which can
  be registered with the main CLI through a call to `cli_add_main_ctx`.

* module initialization: the `foo` application must implement two
  module init functions:

	- `foo_init`: should initialize global data to be used by the application
	  (e.g., CLI, statistics storage). `foo_init` should be called directly
	  from the `main` WARP17 function where all modules are initialized.

	- `foo_lcore_init`: should initalize per core global data to be used by the
	  application (e.g., per core pointers to the statistics corresponding to
	  the current core). `foo_lcore_init` should be called from
	  `pkt_receive_loop` where all modules are initialized.

* example config: ideally, applications should also provide some
  configuration examples which could go to the `examples/` directory.

* .dot file: applications will most likely be implemented as state machines.
  A `.dot` file describing the state machine should be added to the
  `dot/` directory

* tests: any new application shouldn't break any existing tests and __must__
  have it's own tests:

	- a configuration and functionality test file in `ut/test_foo.py` which
	  should try to extensively cover all the code introduced by the
	  application

	- one or more scaling test entries (method) in `ut/test_perf.py`
	  (class TestPerf) which should define the desired performance/scalability
	  values.

* commit messages: please make sure that commit messages follow the
  `.git-commit.template` provided in the repository. In order to enforce this
  template locally you can execute the following command:

```
git config commit.template ./.git-commit.template
```
