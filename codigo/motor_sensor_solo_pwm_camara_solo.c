+---------------------------------------+
|  A A A A A  D D D D   E E E E E       |
| A         A D       D E               |
| A         A D        D E              |
| A A A A A A D        D E E E E E      |
| A         A D        D E              |
| A         A D       D  E              |
| A         A D D D D   E E E E E       |
+---------------------------------------+

------------------------------------------------------------------------------------------------------

Autor: Jon Zorriketa y Adrián Caballero

Fecha: 16-05-24

Titulación: Máster en Sistemas Electrónicos Avanzados

Asignatura: Análisis y Desarrollo de Estrucuturas de Software par SoPC (ADE) (23-24)

Resumen: Este proyecto ha tenido como objetivo controlar un motor desde el telefóno móvil utilizando un sistema de servidor-cliente UDP. 
Además, al contar con un sensor de distancia, se ofrece cierto nivel de seguridad para ayudar a evitar riesgos. Esto se consigue ya que 
si el sensor detecta algún elemento a una distancia considerada como peligrosa, el motor se detiene. Dependiendo de la distancia a la que 
se detecta el obstáculo, el motor se puede volver a arrancar o no. En el caso en el que se considere demasiado peligroso que se vuelva a 
arrancar el motor, se da un aviso de una manera sonora y se prodece a sacar una foto de lo que se está detectando.

--------------------------------------------------------------------------------------------------------



#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <pigpio.h>

// Definición de los pines de GPIO para el motor
#define IN1 5
#define IN2 6
#define IN3 13
#define IN4 19

// Definición de los pines de GPIO para el buzzer
#define BUZZER 4

// Definición de los pines de GPIO para el sensor ultrasónico
#define TRIG 23
#define ECHO 24

volatile int keep_running = 0; // Variable que indica si el motor está parado o arrancado
volatile int manual_override = 0; // Variable para control manual
volatile int ignore_proximity = 0; // Variable para ignorar la proximidad
volatile int received_yes = 0; // Variable para controlar si se ha recibido "yes"
volatile int emergency_stop = 0; // Variable para controlar si hay una parada de emergencia
volatile int mensaje_enviado = 0; // Variable para controlar el numero de mensajes para 30cm
volatile int mensaje_emergencia = 0; // Variable para controlar el numero de mensajes para emergencia

// Variables para crear los hilos, motor_thread para crear el hilo para el motor y sensor_thread para crear el hilo para el sensor
pthread_t motor_thread, sensor_thread;

// Variables para la creación del servidor UDP
int server_fd;
struct sockaddr_in client_addr;
socklen_t addr_len;

// Función para inicializar y configurar los GPIOs
void setup() {
    if (gpioInitialise() < 0) {
        fprintf(stderr, "Failed to initialise GPIO\n");
        exit(1);
    }

    // Configuración de los pines del motor
    gpioSetMode(IN1, PI_OUTPUT);
    gpioSetMode(IN2, PI_OUTPUT);
    gpioSetMode(IN3, PI_OUTPUT);
    gpioSetMode(IN4, PI_OUTPUT);

    // Configuración de los pines del sensor ultrasónico
    gpioSetMode(TRIG, PI_OUTPUT);
    gpioSetMode(ECHO, PI_INPUT);

    // Configuración de los pines del buzzer
    gpioSetMode(BUZZER, PI_OUTPUT);
}

// Función para asignar los valores a, b, c y d a los GPIOs del motor
void set_step(int a, int b, int c, int d) {
    gpioWrite(IN1, a);
    gpioWrite(IN2, b);
    gpioWrite(IN3, c);
    gpioWrite(IN4, d);
}

//void capture_photo() {
    //system("libcamera-still -o /home/rpi-ade/ADE/emergency.jpeg"); // Capturar la foto y guardarla en la ruta especificada
//}

//Función que se encarga de mover el motor
void* motor_control(void* arg) {
    int step_index = 0;
    int steps[8][4] = {
        {1, 0, 0, 0},
        {1, 1, 0, 0},
        {0, 1, 0, 0},
        {0, 1, 1, 0},
        {0, 0, 1, 0},
        {0, 0, 1, 1},
        {0, 0, 0, 1},
        {1, 0, 0, 1}
    };

    while (1) {
        // Comprueba los flags keep_running y manual_override para actuar sobre el motor
        if (keep_running && !manual_override) {
            set_step(steps[step_index][0], steps[step_index][1], steps[step_index][2], steps[step_index][3]);
            step_index = (step_index + 1) % 8;
            usleep(2000);  // Ajusta el retardo para cambiar la velocidad del motor
        } else {
            set_step(0, 0, 0, 0);
            usleep(1000);
        }
    }

    return NULL;
}


// Función para obtener la distancia a la que se detecta un obstaculo
// Además, se toman decisiones en función de la distancia a la que se detecta el obstáculo
void* sensor_monitor(void* arg) {
    char message[1024];

    while (1) {
        // Enviar pulso de disparo
        gpioWrite(TRIG, PI_LOW);
        usleep(2);
        gpioWrite(TRIG, PI_HIGH);
        usleep(10);
        gpioWrite(TRIG, PI_LOW);

        // Medir duración del pulso de respuesta
        while (gpioRead(ECHO) == PI_LOW);
        uint32_t start_time = gpioTick();
        while (gpioRead(ECHO) == PI_HIGH);
        uint32_t end_time = gpioTick();

        // Calcular distancia en cm
        double distance = (end_time - start_time) / 58.0;

        if (distance < 80.0) {
            if (distance < 40.0) {

                // Comprobación para ejecutar si se detecta un elemento a menos 40 cm mientras el motor está en funcionamiento
                if (mensaje_emergencia == 0 && keep_running == 1) {
                    emergency_stop = 1;
                    manual_override = 1;
                    strcpy(message, "Motor parado por emergencia."); 
                    mensaje_emergencia = 1;
                    gpioPWM(BUZZER, 255); // Activar buzzer con 50% de duty cycle
                    system("libcamera-still -o /home/rpi-ade/ADE/emergency2.jpeg"); // Capturar la foto y guardarla en la ruta especificada
                    //capture_photo(); // Capturar foto en parada de emergencia
                    sendto(server_fd, message, strlen(message), MSG_CONFIRM, (struct sockaddr *)&client_addr, addr_len);

                // Comprobación para ejecutar si se detecta un elemento a menos de 40 cm mientras el motor no está en funcionamiento
                } else if (mensaje_emergencia == 0 && keep_running == 0) {
                    emergency_stop = 1;
                    manual_override = 1;
                    strcpy(message, "No es posible arrancar el motor por proximidad.");
                    mensaje_emergencia = 1;
                    gpioPWM(BUZZER, 255); // Activar buzzer con 50% de duty cycle
                    system("libcamera-still -o /home/rpi-ade/ADE/emergency2.jpeg"); // Capturar la foto y guardarla en la ruta especificada
                    //capture_photo(); // Capturar foto en parada de emergencia
                    sendto(server_fd, message, strlen(message), MSG_CONFIRM, (struct sockaddr *)&client_addr, addr_len);
                }
            } else {

                // Comprobación para ejecutar si se detecta un elemento entre 80 y 40 cm mientras el motor no está en funcionamiento
                if (keep_running == 0) {
                    keep_running = 0;
                    manual_override = 1;
                    emergency_stop = 0;
                    if (mensaje_enviado == 0) {
                        strcpy(message, "Cuidado al arrancar, se ha detectado algo cerca, si desea arrancar igualmeten, escribe un yes");
                        mensaje_enviado = 1;
                        sendto(server_fd, message, strlen(message), MSG_CONFIRM, (struct sockaddr *)&client_addr, addr_len);
                    }

                // Comprobación para ejecutar si se detecta un elemento entre 80 y 40 cm mientras el motor está en funcionamiento
                } else if (keep_running == 1) {
                    keep_running = 0;
                    manual_override = 1;
                    emergency_stop = 0;
                    if (mensaje_enviado == 0) {
                        strcpy(message, "Objeto detectado a menos de 40cm. ¿Desea continuar? (yes/no)");
                        mensaje_enviado = 1;
                        sendto(server_fd, message, strlen(message), MSG_CONFIRM, (struct sockaddr *)&client_addr, addr_len);
                    }
                }
            }
        } else {
            emergency_stop = 0;
            gpioPWM(BUZZER, 0); // Apagar buzzer
        }

        usleep(100000);  // Esperar 100ms antes de la siguiente medición
    }

    return NULL;
}

// Función encargada de la gestión del servidor UDP 
int main() {
    int recv_len;
    struct sockaddr_in server_addr;
    char buffer[1024];
    addr_len = sizeof(client_addr);

    setup();

    // Creación del socket UDP
    if ((server_fd = socket(AF_INET, SOCK_DGRAM, 0)) < 0) {
        perror("socket creation failed");
        exit(EXIT_FAILURE);
    }

    memset(&server_addr, 0, sizeof(server_addr));
    memset(&client_addr, 0, sizeof(client_addr));

    // Configuración del servidor
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(6000);

    // Enlazar el socket con la dirección del servidor
    if (bind(server_fd, (const struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        perror("bind failed");
        exit(EXIT_FAILURE);
    }

    // Enviar mensaje inicial solicitando comando start/stop
    char initial_message[] = "Ingrese 'start' para arrancar o 'stop' para detener el motor.";
    sendto(server_fd, initial_message, strlen(initial_message), MSG_CONFIRM, (struct sockaddr *)&client_addr, addr_len);

    // Crear hilos para el control del motor y el monitoreo del sensor
    pthread_create(&motor_thread, NULL, motor_control, NULL);
    pthread_create(&sensor_thread, NULL, sensor_monitor, NULL);

    while (1) {
        recv_len = recvfrom(server_fd, buffer, 1024, 0, (struct sockaddr *) &client_addr, &addr_len);
        buffer[recv_len] = '\0';  // Null-terminate string
        printf("Received: %s\n", buffer);

        if (emergency_stop) {
            char safety_message[] = "No se puede iniciar por seguridad.";
            sendto(server_fd, safety_message, strlen(safety_message), MSG_CONFIRM, (struct sockaddr *)&client_addr, addr_len);
            continue;
        }

        // Condicones dependiendo de lo que se recibe desde el cliente UDP de manera remota ( desde el móvil )
        if (strncmp(buffer, "start", 5) == 0) {
            keep_running = 1;  // Arrancar motor
            ignore_proximity = 0; // Reiniciar la detección de proximidad
        } else if (strncmp(buffer, "stop", 4) == 0) {
            keep_running = 0;  // Detener motor
            ignore_proximity = 0; // Reiniciar la detección de proximidad
        } else if (!received_yes && strncmp(buffer, "yes", 3) == 0) {
            ignore_proximity = 1;  // Ignorar proximidad en el futuro
            keep_running = 1;
            manual_override = 0; // Respuesta recibida, resetear override
            received_yes = 1; // Marcar que se ha recibido "yes"
        } else if (!received_yes && strncmp(buffer, "no", 2) == 0) {
            keep_running = 0;
            manual_override = 0; // Respuesta recibida, resetear override
        } else {
            char invalid_command[] = "Comando no válido. Use 'start' o 'stop'.";
            sendto(server_fd, invalid_command, strlen(invalid_command), MSG_CONFIRM, (struct sockaddr *)&client_addr, addr_len);
            continue;
        }

        // Enviar eco de vuelta:
        sendto(server_fd, buffer, strlen(buffer), MSG_CONFIRM, (struct sockaddr *)&client_addr, addr_len);
    }

    close(server_fd);
    gpioTerminate();  // Limpieza de los recursos de GPIO
    return 0;
}
