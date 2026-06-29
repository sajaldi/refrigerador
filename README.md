# Refrigerador GE GSMS6FGDFSS — Control con ESP32

Restitución total del control de una refrigeradora General Electric modelo GSMS6FGDFSS usando ESP32, con ciclo automático por tiempo, pantalla OLED, control web y configuración remota vía GitHub.

## Hardware

| Componente | Pin ESP32 |
|---|---|
| Compresor (relé) | GPIO2 |
| Ventilador (relé) | GPIO4 |
| Resistencia de deshielo (relé) | GPIO5 |
| Termistor NTC (divisor 15kΩ + 35kΩ@25°C, B=3950) | GPIO34 (ADC) |
| Pantalla OLED SSD1306 128×64 I2C | SDA=GPIO21, SCL=GPIO22 |

## Ciclo automático

- **7h55min** de enfriamiento (compresor + ventilador encendidos)
- **5min** de deshielo (resistencia encendida, compresor y ventilador apagados)
- Ciclo total: **8 horas**

Los tiempos se obtienen desde un archivo JSON en GitHub al arrancar y cada hora, permitiendo modificarlos sin reprogramar el ESP32.

## Configuración remota

El ESP32 lee los tiempos desde:
```
https://raw.githubusercontent.com/sajaldi/refrigerador/main/refrigeradora_config.json
```

Para cambiar los tiempos, edita `refrigeradora_config.json` en GitHub. El ESP32 tomará los nuevos valores en la próxima hora o al reiniciar.

### Formato del JSON

```json
{
  "coolingMinutes": 475,
  "defrostMinutes": 5,
  "startupSeconds": 5
}
```

## Funciones vía web

El ESP32 levanta un servidor web en su IP. Permite:
- Ver temperatura actual, estado del ciclo y tiempo restante
- Encender/apagar compresor, ventilador y resistencia manualmente
- Calibrar el sensor de temperatura (offset)
- La página se refresca automáticamente cada 10 segundos

## Calibración del sensor

Usar la página web: ingresar la temperatura real medida con un termómetro de referencia. El ESP32 calcula y guarda el offset automáticamente en memoria no volátil.
