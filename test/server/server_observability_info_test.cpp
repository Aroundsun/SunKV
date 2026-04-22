#include "test/server/server_test_helper.h"

#include <cstdlib>
#include <string>
#include <vector>

using namespace server_test;

static bool must(bool cond) {
    return cond;
}

static std::string recv(RespStreamReader& reader) {
    const std::string resp = reader.recvOneRESP(std::chrono::seconds(3));
    if (!must(!resp.empty())) {
        return std::string();
    }
    return resp;
}

static bool hasMetric(const std::string& body, const std::string& key) {
    return body.find(key + "=") != std::string::npos;
}

static long long readMetric(const std::string& body, const std::string& key) {
    const std::string prefix = key + "=";
    const std::size_t begin = body.find(prefix);
    if (begin == std::string::npos) {
        return -1;
    }
    std::size_t value_begin = begin + prefix.size();
    std::size_t value_end = body.find('\n', value_begin);
    if (value_end == std::string::npos) {
        value_end = body.size();
    }
    const std::string value = body.substr(value_begin, value_end - value_begin);
    return std::atoll(value.c_str());
}

int main() {
    ServerFixture fixture("server_observability_info_test");
    fixture.cfg.enable_slowlog = true;
    fixture.cfg.slowlog_threshold_ms = 0;
    fixture.start();

    const int fd = connectToHost(fixture.host, fixture.port, std::chrono::seconds(2));
    if (!must(fd >= 0)) return 10;
    RespStreamReader reader(fd);

    if (!must(sendAll(fd, respArrayBulk(std::vector<std::string>{"SET", "obs:key", "v"})))) return 11;
    if (!must(recv(reader) == "+OK\r\n")) return 12;

    if (!must(sendAll(fd, respArrayBulk(std::vector<std::string>{"NO_SUCH_CMD"})))) return 13;
    const std::string unknown_resp = recv(reader);
    if (!must(unknown_resp.find("-ERR") == 0)) return 14;

    if (!must(sendAll(fd, respArrayBulk(std::vector<std::string>{"STATS"})))) return 15;
    const std::string stats_resp = recv(reader);

    RESPParser parser;
    auto parse_result = parser.parse(stats_resp);
    if (!must(parse_result.success && parse_result.complete && parse_result.value)) return 16;
    if (!must(parse_result.value->isBulkString())) return 17;

    const std::string report = parse_result.value->toString();
    if (!must(hasMetric(report, "total_command_errors"))) return 18;
    if (!must(hasMetric(report, "total_slow_commands"))) return 19;
    if (!must(hasMetric(report, "avg_command_latency_us"))) return 20;
    if (!must(hasMetric(report, "max_command_latency_us"))) return 21;
    if (!must(hasMetric(report, "max_conn_input_buffer_bytes"))) return 22;
    if (!must(hasMetric(report, "pubsub_publish_total"))) return 23;
    if (!must(hasMetric(report, "pubsub_delivered_total"))) return 24;

    if (!must(readMetric(report, "total_command_errors") >= 1)) return 25;
    if (!must(readMetric(report, "total_slow_commands") >= 1)) return 26;
    if (!must(readMetric(report, "max_conn_input_buffer_bytes") > 0)) return 27;

    ::close(fd);
    fixture.stop();
    return 0;
}
