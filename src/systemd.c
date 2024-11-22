#include "collectd.h"

#include "liboconfig/oconfig.h"
#include "metric.h"
#include "plugin.h"

#include "systemd/sd-bus.h"
#include "utils/common/common.h"

typedef struct systemd_metric systemd_metric;
struct systemd_metric {
  char *name;
  char const *dbus_type;
  metric_type_t collectd_type;
};

typedef struct systemd_metric_group systemd_metric_group;
struct systemd_metric_group {
  char const *accounting_flag;
  systemd_metric *metrics;
};

systemd_metric_group const groups[] = {
    {
        .accounting_flag = "MemoryAccounting",
        .metrics =
            (systemd_metric[]){
                {
                    .name = "MemoryAvailable",
                    .dbus_type = "t",
                    .collectd_type = METRIC_TYPE_GAUGE,
                },
                {
                    .name = "MemoryCurrent",
                    .dbus_type = "t",
                    .collectd_type = METRIC_TYPE_GAUGE,
                },
                {
                    .name = "MemoryPeak",
                    .dbus_type = "t",
                    .collectd_type = METRIC_TYPE_GAUGE,
                },
                {
                    .name = "MemorySwapCurrent",
                    .dbus_type = "t",
                    .collectd_type = METRIC_TYPE_GAUGE,
                },
                {
                    .name = "MemoryZSwapCurrent",
                    .dbus_type = "t",
                    .collectd_type = METRIC_TYPE_GAUGE,
                },
                {
                    .name = "MemorySwapPeak",
                    .dbus_type = "t",
                    .collectd_type = METRIC_TYPE_GAUGE,
                },
                {.name = NULL},
            },
    },
    {
        .accounting_flag = "IOAccounting",
        .metrics =
            (systemd_metric[]){
                {
                    .name = "IOReadBytes",
                    .dbus_type = "t",
                    .collectd_type = METRIC_TYPE_COUNTER,
                },
                {
                    .name = "IOReadOperations",
                    .dbus_type = "t",
                    .collectd_type = METRIC_TYPE_COUNTER,
                },
                {
                    .name = "IOWriteBytes",
                    .dbus_type = "t",
                    .collectd_type = METRIC_TYPE_COUNTER,
                },
                {
                    .name = "IOWriteOperations",
                    .dbus_type = "t",
                    .collectd_type = METRIC_TYPE_COUNTER,
                },
                {.name = NULL},
            },
    },
    {
        .accounting_flag = "CPUAccounting",
        .metrics =
            (systemd_metric[]){
                {
                    .name = "CPUUsageNSec",
                    .dbus_type = "t",
                    .collectd_type = METRIC_TYPE_GAUGE,
                },
                {.name = NULL},
            },
    },
    {
        .accounting_flag = "IPAccounting",
        .metrics =
            (systemd_metric[]){
                {
                    .name = "IPEgressBytes",
                    .dbus_type = "t",
                    .collectd_type = METRIC_TYPE_COUNTER,
                },
                {
                    .name = "IPEgressPackets",
                    .dbus_type = "t",
                    .collectd_type = METRIC_TYPE_COUNTER,
                },
                {
                    .name = "IPIngressBytes",
                    .dbus_type = "t",
                    .collectd_type = METRIC_TYPE_COUNTER,
                },
                {
                    .name = "IPIngressPackets",
                    .dbus_type = "t",
                    .collectd_type = METRIC_TYPE_COUNTER,
                },
                {.name = NULL},
            },
    },
    {
        .accounting_flag = "TasksAccounting",
        .metrics =
            (systemd_metric[]){
                {
                    .name = "TasksCurrent",
                    .dbus_type = "t",
                    .collectd_type = METRIC_TYPE_GAUGE,
                },
                {.name = NULL},
            },
    },
    {
        .accounting_flag = "true",
        .metrics =
            (systemd_metric[]){
                {
                    .name = "NRestarts",
                    .dbus_type = "u",
                    .collectd_type = METRIC_TYPE_COUNTER,
                },
                {.name = NULL},
            },
    },
    {.accounting_flag = NULL},
};

static char **services = NULL;

static size_t services_num = 0;

static int systemd_config(oconfig_item_t *ci) {
  services_num += ci->children_num;
  services = realloc(services, sizeof(char *) * services_num);
  if (services == NULL) {
    ERROR("Can't allocate memory for services");
    return EXIT_FAILURE;
  }
  for (size_t i = 0; i < ci->children_num; ++i) {
    oconfig_item_t *child = ci->children + i;
    char *external_id = NULL;
    char *service;
    if (cf_util_get_string(child, &external_id) < 0) {
      ERROR("Error during parsing config");
      return EXIT_FAILURE;
    }
    int r = sd_bus_path_encode("/org/freedesktop/systemd1/unit", external_id,
                               &service);
    if (r < 0) {
      ERROR("Can't encode \"%s\" service: %s", external_id, strerror(-r));
      return EXIT_FAILURE;
    }
    services[services_num - ci->children_num + i] = service;
  }
  return EXIT_SUCCESS;
}

sd_bus *bus = NULL;

static int get_prop(sd_bus *bus, char const *service, char const type[static 1],
                    char const prop[static 1], void *var, sd_bus_error *err) {
  sd_bus_message *m = NULL;
  int r = sd_bus_get_property(bus, "org.freedesktop.systemd1", service,
                              "org.freedesktop.systemd1.Service", prop, err, &m,
                              type);
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
  for (char **service_it = services; service_it != services + services_num;
       ++service_it) {
    for (systemd_metric_group const *groups_it = groups;
         groups_it->accounting_flag != NULL; ++groups_it) {
      bool accounting_flag_var = true;
      if (strcmp(groups_it->accounting_flag, "true")) {
        r = get_prop(bus, *service_it, "b", groups_it->accounting_flag,
                     &accounting_flag_var, &sd_bus_err);
        if (r < 0) {
          ERROR("Failed to get %s accounting flag: %s {%s}, %s",
                groups_it->accounting_flag, sd_bus_err.name, sd_bus_err.message,
                strerror(-r));
          goto fail;
        }
      }
      if (accounting_flag_var) {
        for (systemd_metric *metrics_it = groups_it->metrics;
             metrics_it->name != NULL; ++metrics_it) {
          uint64_t val;
          r = get_prop(bus, *service_it, metrics_it->dbus_type,
                       metrics_it->name, &val, &sd_bus_err);
          if (r < 0) {
            ERROR("Failed to get %s property: %s {%s}, %s", metrics_it->name,
                  sd_bus_err.name, sd_bus_err.message, strerror(-r));
            goto fail;
          }

          metric_family_t fam = {
              .name = metrics_it->name,
              .type = metrics_it->collectd_type,
          };
          metric_t m;
          switch (metrics_it->collectd_type) {
          case METRIC_TYPE_COUNTER:
            m = (metric_t){.value.counter = val};
            break;
          case METRIC_TYPE_GAUGE:
            m = (metric_t){.value.gauge = val};
            break;
          default:
            ERROR("Unimplemented collectd type");
            goto fail;
          }
          metric_label_set(&m, "path", *service_it);
          metric_family_metric_append(&fam, m);
          r = plugin_dispatch_metric_family(&fam);
          metric_family_metric_reset(&fam);
          if (r != 0) {
            ERROR("Failed to dispatch: %s", STRERROR(r));
            goto fail;
          }
        }
      }
    }
  }
  return EXIT_SUCCESS;

fail:
  sd_bus_error_free(&sd_bus_err);
  return r;
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
  for (char **service_it = services; *service_it != NULL; ++service_it) {
    free(*service_it);
  }
  free(services);
  return EXIT_SUCCESS;
}

void module_register() {
  plugin_register_init("systemd", systemd_init);
  plugin_register_complex_config("systemd", systemd_config);
  plugin_register_read("systemd", systemd_read);
  plugin_register_shutdown("systemd", systemd_shutdown);
}
