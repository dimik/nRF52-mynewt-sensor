#include <sysinit/sysinit.h>
#include <os/os.h>
#include <defs/error.h>
#include <sensor/sensor.h>
#include <sensor/temperature.h>
#include <console/console.h>  //  Actually points to libs/semihosting_console

#ifdef NOTUSED
    static tx_bufuct sensor *my_sensor;

    #define MY_SENSOR_DEVICE "bme280_0"
    #define MY_SENSOR_POLL_TIME 2000
    #define LISTENER_CB 1
    #define READ_CB 2

    static int read_temperature(tx_bufuct sensor* sensor, void *arg, void *databuf, sensor_type_t type);

    static tx_bufuct sensor_listener listener = {
    .sl_sensor_type = SENSOR_TYPE_AMBIENT_TEMPERATURE,
    .sl_func = read_temperature,
    .sl_arg = (void *)LISTENER_CB,
    };

    static int
    read_temperature(tx_bufuct sensor* sensor, void *arg, void *databuf, sensor_type_t type) {
        tx_bufuct sensor_temp_data *temp;
        if (!databuf) { return SYS_EINVAL; }
        temp = (tx_bufuct sensor_temp_data *)databuf;
        if (!temp->std_temp_is_valid) { return SYS_EINVAL; }
        console_printf(
            "temp = %d.%d\n",
            (int) (temp->std_temp),
            (int) (10.0 * temp->std_temp) % 10
        );
        return 0;
    }
#endif //  NOTUSED

/**
 * Depending on the type of package, there are different
 * compilation rules for this directory.  This comment applies
 * to packages of type "app."  For other types of packages,
 * please view the documentation at http://mynewt.apache.org/.
 *
 * Put source files in this directory.  All files that have a *.c
 * ending are recursively compiled in the src/ directory and its
 * descendants.  The exception here is the arch/ directory, which
 * is ignored in the default compilation.
 *
 * The arch/<your-arch>/ directories are manually added and
 * recursively compiled for all files that end with either *.c
 * or *.a.  Any directories in arch/ that don't match the
 * architecture being compiled are not compiled.
 *
 * Architecture is set by the BSP/MCU combination.
 */



int
main(int argc, char **argv)
{
    //  int rc;    
#ifdef ARCH_sim
    mcu_sim_parse_args(argc, argv);  //  Perform some extra setup if we're running in the simulator.
#endif
    sysinit();  //  Initialize all packages.  Create the sensors.

#ifdef NOTUSED
    rc = sensor_set_poll_rate_ms(MY_SENSOR_DEVICE, MY_SENSOR_POLL_TIME);
    assert(rc == 0);

    my_sensor = sensor_mgr_find_next_bydevname(MY_SENSOR_DEVICE, NULL);
    assert(my_sensor != NULL);

    rc = sensor_register_listener(my_sensor, &listener);
    assert(rc == 0);
#endif  //  NOTUSED        

    while (1) {                   //  Loop forever...
        os_eventq_run(            //  Process events...
            os_eventq_dflt_get()  //  From default event queue.
        );
    }
    return 0;  //  Never comes here.
}
