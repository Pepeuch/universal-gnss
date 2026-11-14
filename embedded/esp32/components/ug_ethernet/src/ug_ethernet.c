// SPDX-License-Identifier: LGPL-3.0-only
#include "ug_ethernet.h"
#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "esp_eth.h"
#include "esp_eth_mac_w5500.h"
#include "esp_eth_phy_w5500.h"
#include "esp_event.h"
#include "esp_mac.h"
#include "esp_netif.h"
#include "freertos/FreeRTOS.h"
#include "freertos/portmacro.h"

static esp_eth_handle_t ethernet;
static esp_netif_t* netif;
static esp_eth_netif_glue_handle_t glue;
static portMUX_TYPE lock = portMUX_INITIALIZER_UNLOCKED;
static bool ready;
static void event(void* arg, esp_event_base_t base, int32_t id, void* data)
{
  (void)arg;
  (void)data;
  if ((base == ETH_EVENT && id == ETHERNET_EVENT_DISCONNECTED) ||
      (base == IP_EVENT && id == IP_EVENT_ETH_LOST_IP))
  {
    portENTER_CRITICAL(&lock);
    ready = false;
    portEXIT_CRITICAL(&lock);
  }
  else if (base == IP_EVENT && id == IP_EVENT_ETH_GOT_IP)
  {
    esp_netif_set_default_netif(netif);
    portENTER_CRITICAL(&lock);
    ready = true;
    portEXIT_CRITICAL(&lock);
  }
}
bool ug_ethernet_ready(void)
{
  portENTER_CRITICAL(&lock);
  bool value = ready;
  portEXIT_CRITICAL(&lock);
  return value;
}
esp_err_t ug_ethernet_start_w5500(const ug_ethernet_pins_t* pins)
{
  if (!pins || ethernet)
    return ESP_ERR_INVALID_STATE;
  const int all[] = {
      pins->mosi, pins->miso, pins->clock, pins->chip_select, pins->interrupt, pins->reset};
  for (unsigned i = 0; i < sizeof(all) / sizeof(all[0]); ++i)
  {
    if (!GPIO_IS_VALID_GPIO(all[i]) ||
        ((i == 0 || i == 2 || i == 3 || i == 5) && !GPIO_IS_VALID_OUTPUT_GPIO(all[i])))
      return ESP_ERR_INVALID_ARG;
    for (unsigned j = 0; j < i; ++j)
      if (all[i] == all[j])
        return ESP_ERR_INVALID_ARG;
  }
  esp_err_t error = gpio_install_isr_service(0);
  if (error != ESP_OK && error != ESP_ERR_INVALID_STATE)
    return error;
  spi_bus_config_t bus = {.mosi_io_num = pins->mosi,
                          .miso_io_num = pins->miso,
                          .sclk_io_num = pins->clock,
                          .quadwp_io_num = -1,
                          .quadhd_io_num = -1,
                          .max_transfer_sz = 2048};
  error = spi_bus_initialize(SPI2_HOST, &bus, SPI_DMA_CH_AUTO);
  if (error != ESP_OK)
    return error;
  spi_device_interface_config_t device = {
      .mode = 0, .clock_speed_hz = 20000000, .spics_io_num = pins->chip_select, .queue_size = 16};
  eth_w5500_config_t wiz = ETH_W5500_DEFAULT_CONFIG(SPI2_HOST, &device);
  wiz.base.int_gpio_num = pins->interrupt;
  eth_mac_config_t mac_config = ETH_MAC_DEFAULT_CONFIG();
  eth_phy_config_t phy_config = ETH_PHY_DEFAULT_CONFIG();
  phy_config.phy_addr = 1;
  phy_config.reset_gpio_num = pins->reset;
  esp_eth_mac_t* mac = esp_eth_mac_new_w5500(&wiz, &mac_config);
  esp_eth_phy_t* phy = esp_eth_phy_new_w5500(&phy_config);
  if (!mac || !phy)
  {
    if (mac)
      mac->del(mac);
    if (phy)
      phy->del(phy);
    spi_bus_free(SPI2_HOST);
    return ESP_ERR_NO_MEM;
  }
  esp_eth_config_t config = ETH_DEFAULT_CONFIG(mac, phy);
  error = esp_eth_driver_install(&config, &ethernet);
  if (error != ESP_OK)
  {
    mac->del(mac);
    phy->del(phy);
    spi_bus_free(SPI2_HOST);
    return error;
  }
  uint8_t wifi_mac[6], local_mac[6];
  error = esp_read_mac(wifi_mac, ESP_MAC_WIFI_STA);
  if (error == ESP_OK)
    error = esp_derive_local_mac(local_mac, wifi_mac);
  if (error == ESP_OK)
    error = esp_eth_ioctl(ethernet, ETH_CMD_S_MAC_ADDR, local_mac);
  if (error != ESP_OK)
    goto fail;
  esp_netif_config_t netif_config = ESP_NETIF_DEFAULT_ETH();
  netif = esp_netif_new(&netif_config);
  glue = esp_eth_new_netif_glue(ethernet);
  if (!netif || !glue)
  {
    error = ESP_ERR_NO_MEM;
    goto fail;
  }
  error = esp_netif_attach(netif, glue);
  if (error != ESP_OK)
    goto fail;
  error = esp_event_handler_register(ETH_EVENT, ESP_EVENT_ANY_ID, event, NULL);
  if (error != ESP_OK)
    goto fail;
  error = esp_event_handler_register(IP_EVENT, IP_EVENT_ETH_GOT_IP, event, NULL);
  if (error != ESP_OK)
    goto fail;
  error = esp_event_handler_register(IP_EVENT, IP_EVENT_ETH_LOST_IP, event, NULL);
  if (error != ESP_OK)
    goto fail;
  error = esp_eth_start(ethernet);
  if (error == ESP_OK)
    return ESP_OK;
fail:
  esp_event_handler_unregister(ETH_EVENT, ESP_EVENT_ANY_ID, event);
  esp_event_handler_unregister(IP_EVENT, IP_EVENT_ETH_GOT_IP, event);
  esp_event_handler_unregister(IP_EVENT, IP_EVENT_ETH_LOST_IP, event);
  if (glue)
  {
    esp_eth_del_netif_glue(glue);
    glue = NULL;
  }
  if (netif)
  {
    esp_netif_destroy(netif);
    netif = NULL;
  }
  esp_eth_driver_uninstall(ethernet);
  ethernet = NULL;
  mac->del(mac);
  phy->del(phy);
  spi_bus_free(SPI2_HOST);
  return error;
}
