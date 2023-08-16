/* SD card and FAT filesystem example.
   This example uses SPI peripheral to communicate with SD card.

   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/

#include <string.h>
#include <sys/unistd.h>
#include <sys/stat.h>
#include "esp_vfs_fat.h"
#include "sdmmc_cmd.h"
#include "time.h"
#include "driver/i2c.h"

#define EXAMPLE_MAX_CHAR_SIZE 64

static const char *TAG = "MicroSD";

#define MOUNT_POINT "/sdcard"

// Pin assignments SD
#define PIN_NUM_MISO 37
#define PIN_NUM_MOSI 35
#define PIN_NUM_CLK 36
#define PIN_NUM_CS 38
// Pin assignments I2C(RTC)
#define I2C_SLAVE_ADDR 0x68
#define I2C_SDA 4
#define I2C_SCL 5


static esp_err_t set_i2c(void)
{
    i2c_config_t i2c_config = {};
    i2c_config.mode = I2C_MODE_MASTER;
    i2c_config.sda_io_num = I2C_SDA;
    i2c_config.scl_io_num = I2C_SCL;
    i2c_config.sda_pullup_en = true;
    i2c_config.scl_pullup_en = true;
    i2c_config.master.clk_speed = 400000;
    i2c_config.clk_flags = 0;

    ESP_ERROR_CHECK(i2c_param_config(I2C_NUM_0, &i2c_config));
    ESP_ERROR_CHECK(i2c_driver_install(I2C_NUM_0, I2C_MODE_MASTER, 0, 0, ESP_INTR_FLAG_LEVEL1));

    return ESP_OK;
}
static esp_err_t ds1307_init(void)
{
    // Obtener la fecha y hora

    uint8_t init_data[] = {
        0x00, // Segundos
        0x15, // Minutos
        0x08, // Horas (12 en formato 24 horas)
        0x03, // Día de la semana (6=Sábado)
        0x16, // Día del mes
        0x08, // Mes (agosto)
        0x23  // Año (2021 en formato de dos dígitos)
    };

    i2c_cmd_handle_t cmd = i2c_cmd_link_create();                                    // Crear un manejador de comandos I2C
    i2c_master_start(cmd);                                                           // Agregar un comando de inicio de comunicación I2C
    i2c_master_write_byte(cmd, (I2C_SLAVE_ADDR << 1) | I2C_MASTER_WRITE, true);      // Escribir dirección del DS1307 en modo escritura
    i2c_master_write_byte(cmd, 0x00, 0x1);                                           // Comenzar en el registro de segundos
    i2c_master_write(cmd, init_data, sizeof(init_data), true);                       // Escribir los valores de inicialización
    i2c_master_stop(cmd);                                                            // Agregar un comando de detención de comunicación I2C
    esp_err_t ret = i2c_master_cmd_begin(I2C_NUM_0, cmd, 1000 / portTICK_PERIOD_MS); // Ejecutar la secuencia de comandos I2C
    i2c_cmd_link_delete(cmd);                                                        // Liberar el manejador de comandos

    if (ret == ESP_OK)
    {
        ESP_LOGI(TAG, "Comunicacion i2c completa");
    }
    else if (ret == ESP_ERR_TIMEOUT)
    {
        ESP_LOGE(TAG, "No envio a tiempo la configuracion");
    }
    else
    {
        ESP_LOGE(TAG, "Error en la comunicacion");
    }

    return ESP_OK;
}
static esp_err_t ds1307_get_time(uint8_t rx_data[7])
{
    uint8_t command = 0x00;

    i2c_master_write_read_device(I2C_NUM_0,
                                 I2C_SLAVE_ADDR,
                                 &command,
                                 1,
                                 rx_data,
                                 7,
                                 pdMS_TO_TICKS(1000));
    return ESP_OK;
}

void app_main(void)
{

//_____________________________________Setting MicroSD Card Adapter___________________________________________________________________
    esp_err_t ret;

    // Options for mounting the filesystem.
    // If format_if_mount_failed is set to true, SD card will be partitioned and
    // formatted in case when mounting fails.
    esp_vfs_fat_sdmmc_mount_config_t mount_config = {
#ifdef CONFIG_EXAMPLE_FORMAT_IF_MOUNT_FAILED
        .format_if_mount_failed = true,
#else
        .format_if_mount_failed = false,
#endif // EXAMPLE_FORMAT_IF_MOUNT_FAILED
        .max_files = 5,
        .allocation_unit_size = 16 * 1024
    };
    sdmmc_card_t *card;
    const char mount_point[] = MOUNT_POINT;
    ESP_LOGI(TAG, "Initializing SD card");

    // Use settings defined above to initialize SD card and mount FAT filesystem.
    // Note: esp_vfs_fat_sdmmc/sdspi_mount is all-in-one convenience functions.
    // Please check its source code and implement error recovery when developing
    // production applications.
    ESP_LOGI(TAG, "Using SPI peripheral");

    // By default, SD card frequency is initialized to SDMMC_FREQ_DEFAULT (20MHz)
    // For setting a specific frequency, use host.max_freq_khz (range 400kHz - 20MHz for SDSPI)
    // Example: for fixed frequency of 10MHz, use host.max_freq_khz = 10000;
    sdmmc_host_t host = SDSPI_HOST_DEFAULT();

    spi_bus_config_t bus_cfg = {
        .mosi_io_num = PIN_NUM_MOSI,
        .miso_io_num = PIN_NUM_MISO,
        .sclk_io_num = PIN_NUM_CLK,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = 4000,
    };
    ret = spi_bus_initialize(host.slot, &bus_cfg, SDSPI_DEFAULT_DMA);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize bus.");
        return;
    }

    // This initializes the slot without card detect (CD) and write protect (WP) signals.
    // Modify slot_config.gpio_cd and slot_config.gpio_wp if your board has these signals.
    sdspi_device_config_t slot_config = SDSPI_DEVICE_CONFIG_DEFAULT();
    slot_config.gpio_cs = PIN_NUM_CS;
    slot_config.host_id = host.slot;

    ESP_LOGI(TAG, "Mounting filesystem");
    ret = esp_vfs_fat_sdspi_mount(mount_point, &host, &slot_config, &mount_config, &card);

    if (ret != ESP_OK) {
        if (ret == ESP_FAIL) {
            ESP_LOGE(TAG, "Failed to mount filesystem. "
                     "If you want the card to be formatted, set the CONFIG_EXAMPLE_FORMAT_IF_MOUNT_FAILED menuconfig option.");
        } else {
            ESP_LOGE(TAG, "Failed to initialize the card (%s). "
                     "Make sure SD card lines have pull-up resistors in place.", esp_err_to_name(ret));
        }
        return;
    }
    ESP_LOGI(TAG, "Filesystem mounted");

    // Card has been initialized, print its properties
    sdmmc_card_print_info(stdout, card);


//____________________Setting I2C for RTC______________________________________________________________________________
    uint8_t RTC_time[7];
    ESP_ERROR_CHECK(set_i2c());
    //ESP_ERROR_CHECK(ds1307_init());
    ds1307_get_time(RTC_time);
//__________________________________________Code for datalogger________________________________________________________
    // Create blackup file
    const char *BACKUP_DATA = MOUNT_POINT "/backup.txt";
    FILE *backup_data = fopen(BACKUP_DATA, "w");
    if (backup_data == NULL)
    {
        ESP_LOGE(TAG, "Failed to open backup file for writing");
    }
    //___________________________________________Read error_____________________________________________________________
    const char *FILE_FAILED = MOUNT_POINT "/failed.txt";
    FILE *file_failed = fopen(FILE_FAILED, "r");
    if (file_failed == NULL)
    {
        ESP_LOGI(TAG, "data file does not exist, creating...");
    }
    else
    {
        ESP_LOGI(TAG, "Data file exists, creating backup");
        char c;
        while (!feof(file_failed))
        {
            c = fgetc(file_failed);
            fputc(c, backup_data);
            printf("%c", c);
        }
        fclose(file_failed);
    }
    //____________________________________________Write error__________________________________________________________ 
    file_failed = fopen(FILE_FAILED, "a");
    if (file_failed == NULL)
    {
        ESP_LOGE(TAG, "Failed to open file for writing");
        return;
    }
    //_____________________________________________Data writing _______________________________________________________
    float temperature = 20.0 + (float)(rand() % 10);
    float pressure = 5.0 + (float)(rand() % 20);
    fprintf(file_failed, "%02x:%02x:%02x  %02x/%02x/%02x ,%.2f,%.2f \n", 
    RTC_time[2], RTC_time[1], RTC_time[0],RTC_time[4], RTC_time[5], RTC_time[6], temperature, pressure);
    fclose(file_failed);
    fclose(backup_data);
    remove(BACKUP_DATA); // Remove backup data
    //____________________All done, unmount partition and disable SPI peripheral_______________________________________
    esp_vfs_fat_sdcard_unmount(mount_point, card);
    ESP_LOGI(TAG, "Card unmounted");

    //deinitialize the bus after all devices are removed
    spi_bus_free(host.slot);
}
