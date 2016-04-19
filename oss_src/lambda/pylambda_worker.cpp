#include <lambda/pylambda_worker.hpp>
#include <cppipc/server/comm_server.hpp>
#include <lambda/pylambda.hpp>
#include <shmipc/shmipc.hpp>
#include <lambda/graph_pylambda.hpp>
#include <logger/logger.hpp>
#include <process/process_util.hpp>
#include <util/try_finally.hpp>

namespace graphlab { namespace lambda {

/** The main function to be called from the python ctypes library to
 *  create a pylambda worker process.
 *
 *  Different error routes produce different error codes of 101 and
 *  above.
 */
int EXPORT pylambda_worker_main(const std::string& root_path,
                                const std::string& server_address, int loglevel) {

  /** Set up the debug configuration.
   *
   *  By default, all LOG_ERROR and LOG_FATAL messages are sent to
   *  stderr, and all messages above loglevel are sent to stdout.
   *
   *  If GRAPHLAB_LAMBDA_WORKER_LOG_FILE is set and is non-empty, then
   *  all log messages are sent to the file instead of the stdout and
   *  stderr.  In this case, the only errors logged to stderr/stdout
   *  concern opening the log file.
   *
   *  If GRAPHLAB_LAMBDA_WORKER_DEBUG_MODE is set, then the default
   *  log level is set to LOG_DEBUG.  If a log file is set, then all
   *  log messages are sent there, otherwise they are sent to stderr.
   */
  boost::optional<std::string> debug_mode_str = graphlab::getenv_str("GRAPHLAB_LAMBDA_WORKER_DEBUG_MODE");
  boost::optional<std::string> debug_mode_file_str = graphlab::getenv_str("GRAPHLAB_LAMBDA_WORKER_LOG_FILE");

  std::string log_file_string = debug_mode_file_str ? *debug_mode_file_str :  "";
  bool log_to_file = (!log_file_string.empty());

  bool debug_mode = (bool)(debug_mode_str);

  global_logger().set_log_level(loglevel);
  global_logger().set_log_to_console(true);

  // Logging using the LOG_DEBUG_WITH_PID macro requires this_pid to be set.
  size_t this_pid = get_my_pid();
  global_logger().set_pid(this_pid);

  // Set up the logging to file if needed.
  if(log_to_file) {
    // Set up the logging to the file, with any errors being fully logged.
    global_logger().set_log_to_console(true, true);
    global_logger().set_log_file(log_file_string);
    LOG_DEBUG_WITH_PID("Logging lambda worker logs to " << log_file_string);
    global_logger().set_log_to_console(false);
  }

  // Now, set the log mode for debug
  if(debug_mode) {
    global_logger().set_log_level(LOG_DEBUG);
    if(!log_to_file) {
      // Set logging to console, with everything logged to stderr.
      global_logger().set_log_to_console(true, true);
    }
  }

  // Log the basic information about parameters.
  size_t parent_pid = get_parent_pid();

  LOG_DEBUG_WITH_PID("root_path = '" << root_path << "'");
  LOG_DEBUG_WITH_PID("server_address = '" << server_address << "'");
  LOG_DEBUG_WITH_PID("parend pid = " << parent_pid);

  size_t _last_line = __LINE__;
#define __TRACK do { _last_line = __LINE__; } while(0)  
  try {

    LOG_DEBUG_WITH_PID("Library function entered successfully.");

    if(server_address == "debug") {
      logstream(LOG_INFO) << "Exiting dry run." << std::endl;
      return 1;
    }

    __TRACK; boost::optional<std::string> disable_shm = graphlab::getenv_str("GRAPHLAB_DISABLE_LAMBDA_SHM");
    bool use_shm = true;
    if(disable_shm && *disable_shm == "1") {
      use_shm = false;
      __TRACK; LOG_DEBUG_WITH_PID("shm disabled.");
    }

    __TRACK; graphlab::shmipc::server shm_comm_server;
    __TRACK; bool has_shm = use_shm ? shm_comm_server.bind() : false;
    
    __TRACK; LOG_DEBUG_WITH_PID("shm_comm_server bind: has_shm=" << has_shm);

    // construct the server
    __TRACK; cppipc::comm_server server(std::vector<std::string>(), "", server_address);

    __TRACK; server.register_type<graphlab::lambda::lambda_evaluator_interface>([&](){
        if (has_shm) {
          __TRACK; auto n = new graphlab::lambda::pylambda_evaluator(&shm_comm_server);
          __TRACK; LOG_DEBUG_WITH_PID("creation of pylambda_evaluator with SHM complete.");
          __TRACK; return n;
        } else {
          __TRACK; auto n = new graphlab::lambda::pylambda_evaluator();
          __TRACK; LOG_DEBUG_WITH_PID("creation of pylambda_evaluator without SHM complete.");
          __TRACK; return n;
        }
      });
    
    __TRACK; server.register_type<graphlab::lambda::graph_lambda_evaluator_interface>([&](){
        __TRACK; auto n = new graphlab::lambda::graph_pylambda_evaluator();
        __TRACK; LOG_DEBUG_WITH_PID("creation of graph_pylambda_evaluator complete.");
        __TRACK; return n;
      });
    
    __TRACK; LOG_DEBUG_WITH_PID("Starting server.");
    __TRACK; server.start();

    __TRACK; wait_for_parent_exit(parent_pid);

    return 0;

    /** Any exceptions happening?  If so, propegate back what's going
     *  on through the error codes.
     */
  } catch (const std::string& error) {
    logstream(LOG_ERROR) << "Internal PyLambda Error: " << error
                         << "; last successful line =" << _last_line  << std::endl;
    return _last_line;
  } catch (const std::exception& error) {
    logstream(LOG_ERROR) << "PyLambda C++ Error: " << error.what()
                         << "; last successful line =" << _last_line << std::endl;
    return _last_line;
  } catch (...) {
    logstream(LOG_ERROR) << "Unknown PyLambda Error"
                         << "; last successful line =" << _last_line << std::endl;
    return _last_line;
  }
}

}}

