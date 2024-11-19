#include "collectd.h"

#include "metric.h"
#include "plugin.h"

#include "systemd/sd-bus.h"
#include "utils/common/common.h"

typedef struct systemd_metric systemd_metric;
struct systemd_metric {
  char *name;
  char const *type;
};

typedef struct systemd_accounting_group systemd_accounting_group;
struct systemd_accounting_group {
  char const *accounting_flag;
  systemd_metric *metrics;
};

systemd_accounting_group const groups[] = {
    {
        .accounting_flag = "MemoryAccounting",
        .metrics =
            (systemd_metric[]){
                {.name = "MemoryAvailable", .type = "t"},
                {.name = "MemoryCurrent", .type = "t"},
                {.name = "MemoryPeak", .type = "t"},
                {.name = "MemorySwapCurrent", .type = "t"},
                {.name = "MemoryZSwapCurrent", .type = "t"},
                {.name = "MemorySwapPeak", .type = "t"},
                {.name = NULL},
            },
    },
    {
        .accounting_flag = "IOAccounting",
        .metrics =
            (systemd_metric[]){
                {.name = "IOReadBytes", .type = "t"},
                {.name = "IOReadOperations", .type = "t"},
                {.name = "IOWriteBytes", .type = "t"},
                {.name = "IOWriteOperations", .type = "t"},
                {.name = NULL},
            },
    },
    {
        .accounting_flag = "CPUAccounting",
        .metrics =
            (systemd_metric[]){
                {.name = "CPUUsageNSec", .type = "t"},
                {.name = NULL},
            },
    },
    {
        .accounting_flag = "IPAccounting",
        .metrics =
            (systemd_metric[]){
                {.name = "IPEgressBytes", .type = "t"},
                {.name = "IPEgressPackets", .type = "t"},
                {.name = "IPIngressBytes", .type = "t"},
                {.name = "IPIngressPackets", .type = "t"},
                {.name = NULL},
            },
    },
    {
        .accounting_flag = "TasksAccounting",
        .metrics =
            (systemd_metric[]){
                {.name = "TasksCurrent", .type = "t"},
                {.name = NULL},
            },
    },
    {
        .accounting_flag = "true",
        .metrics =
            (systemd_metric[]){
                {.name = "NRestarts", .type = "u"},
                {.name = NULL},
            },
    },
    {.accounting_flag = NULL},
};

sd_bus *bus = NULL;

// Err: negative
static int get_prop(sd_bus *bus, char const type[static 1],
                    char const prop[static 1], void *var, sd_bus_error *err) {
  sd_bus_message *m = NULL;
  int r = sd_bus_get_property(
      bus, "org.freedesktop.systemd1",
      "/org/freedesktop/systemd1/unit/avahi_2ddaemon_2eservice",
      "org.freedesktop.systemd1.Service", prop, err, &m, type);
  if (r < 0) {
    return r;
  }
  r = sd_bus_message_read(m, type, var);
  sd_bus_message_unref(m);
  return r;
}

static int systemd_read() {
  int r;
  sd_bus_error sd_bus_err = SD_BUS_ERROR_NULL;
  for (systemd_accounting_group const *groups_it = groups;
       groups_it->accounting_flag != NULL; ++groups_it) {
    bool accounting_flag_var = true;
    if (strcmp(groups_it->accounting_flag, "true")) {
      r = get_prop(bus, "b", groups_it->accounting_flag, &accounting_flag_var,
                   &sd_bus_err);
      if (r < 0) {
        ERROR("Failed to get %s accounting flag: %s {%s}, %s",
              groups_it->accounting_flag, sd_bus_err.name, sd_bus_err.message,
              strerror(-r));
        goto defer;
      }
    }
    if (accounting_flag_var) {
      for (systemd_metric *metrics_it = groups_it->metrics;
           metrics_it->name != NULL; ++metrics_it) {
        uint64_t val;
        r = get_prop(bus, metrics_it->type, metrics_it->name, &val,
                     &sd_bus_err);
        if (r < 0) {
          ERROR("Failed to get %s property: %s {%s}, %s", metrics_it->name,
                sd_bus_err.name, sd_bus_err.message, strerror(-r));
          goto defer;
        }

        metric_family_t fam = {.name = metrics_it->name,
                               .type = METRIC_TYPE_COUNTER};
        metric_family_metric_append(&fam, (metric_t){.value.counter = val});
        r = plugin_dispatch_metric_family(&fam);
        metric_family_metric_reset(&fam);
        if (r != 0) {
          ERROR("Failed to dispatch: %s", STRERROR(r));
          goto defer;
        }
      }
    }
  }

defer:
  sd_bus_error_free(&sd_bus_err);

  return EXIT_SUCCESS;
}

static int systemd_init() {
  if (bus == NULL) {
    int r = sd_bus_open_system(&bus);
    if (r < 0) {
      ERROR("Failed to connect to system bus: %s", strerror(-r));
      sd_bus_unref(bus);
      return r;
    }
  }
  return EXIT_SUCCESS;
}

static int systemd_shutdown() {
  sd_bus_unref(bus);
  return EXIT_SUCCESS;
}

void module_register() {
  plugin_register_init("systemd", systemd_init);
  plugin_register_read("systemd", systemd_read);
  plugin_register_shutdown("systemd", systemd_shutdown);
}
