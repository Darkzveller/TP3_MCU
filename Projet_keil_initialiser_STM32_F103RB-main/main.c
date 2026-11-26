#include "stm32f10x.h"
#include <stdint.h>
#include <stdio.h>
#include <stdarg.h>

typedef enum
{
	GPIO_INPUT_ANALOG = 0x0,
	GPIO_OUTPUT_PP_10MHZ = 0x1,
	GPIO_OUTPUT_PP_2MHZ = 0x2,
	GPIO_OUTPUT_PP_50MHZ = 0x3,

	GPIO_INPUT_FLOATING = 0x4,
	GPIO_OUTPUT_OD_10MHZ = 0x5,
	GPIO_OUTPUT_OD_2MHZ = 0x6,
	GPIO_OUTPUT_OD_50MHZ = 0x7,

	GPIO_INPUT_PULL = 0x8,

	GPIO_AF_PP_10MHZ = 0x9,
	GPIO_AF_PP_2MHZ = 0xA,
	GPIO_AF_PP_50MHZ = 0xB,

	GPIO_AF_OD_10MHZ = 0xD,
	GPIO_AF_OD_2MHZ = 0xE,
	GPIO_AF_OD_50MHZ = 0xF

} GPIO_Config_t;

typedef enum
{
	GPIO_LOW = 0,
	GPIO_HIGH = 1

} GPIO_State_t;

void GPIO_InitPin(GPIO_TypeDef *GPIOx, uint8_t pin, GPIO_Config_t config, GPIO_State_t defaultState)
{
	uint32_t shift;

	if (pin > 15)
		return;

	/* ==========================
	   Activation horloge GPIO
	   ========================== */

	if (GPIOx == GPIOA)
	{
		RCC->APB2ENR |= RCC_APB2ENR_IOPAEN;
	}
	else if (GPIOx == GPIOB)
	{
		RCC->APB2ENR |= RCC_APB2ENR_IOPBEN;
	}
	else if (GPIOx == GPIOC)
	{
		RCC->APB2ENR |= RCC_APB2ENR_IOPCEN;
	}
	else if (GPIOx == GPIOD)
	{
		RCC->APB2ENR |= RCC_APB2ENR_IOPDEN;
	}
	else
	{
		return;
	}

	/* ==========================
	   Etat par d�faut
	   ========================== */

	if (defaultState == GPIO_HIGH)
	{
		GPIOx->BSRR = (1U << pin);
	}
	else
	{
		GPIOx->BRR = (1U << pin);
	}

	/* ==========================
	   Configuration
	   ========================== */

	if (pin < 8)
	{
		shift = pin * 4;

		GPIOx->CRL &= ~(0xFU << shift);
		GPIOx->CRL |= ((uint32_t)config << shift);
	}
	else
	{
		shift = (pin - 8) * 4;

		GPIOx->CRH &= ~(0xFU << shift);
		GPIOx->CRH |= ((uint32_t)config << shift);
	}
}
GPIO_State_t GPIO_ReadPin(GPIO_TypeDef *GPIOx, uint8_t pin)
{
	if (pin > 15)
		return GPIO_LOW;

	// IDR = Input Data Register
	if (GPIOx->IDR & (1U << pin))
	{
		return GPIO_HIGH;
	}
	else
	{
		return GPIO_LOW;
	}
}

void config_usart2(uint32_t cpu_freq_hz, uint32_t baudrate)
{
	uint32_t pclk1_freq;
	uint32_t ppre1;
	uint32_t apb1_div;
	uint32_t usartdiv_x16;
	uint32_t mantissa;
	uint32_t fraction;

	/* =========================================================
	 * 1. CONFIGURATION DES ENTREES / SORTIES DE L'USART2
	 * ========================================================= */

	/* Activation horloge GPIOA */
	RCC->APB2ENR |= (1U << 2);

	/*
	 * USART2 :
	 * PA2 = TX
	 * PA3 = RX
	 *
	 * PA2 :
	 * MODE2 = 01 -> sortie 10 MHz
	 * CNF2  = 10 -> Alternate Function Push-Pull
	 *
	 * PA3 :
	 * MODE3 = 00 -> entrée
	 * CNF3  = 01 -> entrée flottante
	 */

	GPIOA->CRL &= ~(0xFFU << 8);
	GPIOA->CRL |= (0x49U << 8);

	/* =========================================================
	 * 2. ACTIVATION HORLOGE USART2
	 * ========================================================= */

	RCC->APB1ENR |= (1U << 17);

	/* =========================================================
	 * 3. DETERMINATION DE LA FREQUENCE APB1
	 * ========================================================= */

	/*
	 * USART2 est connecté au bus APB1.
	 *
	 * On récupère PPRE1 = bits [10:8] de RCC_CFGR.
	 */

	ppre1 = (RCC->CFGR >> 8) & 0x7U;

	/*
	 * PPRE1 :
	 *
	 * 0xx = HCLK / 1
	 * 100 = HCLK / 2
	 * 101 = HCLK / 4
	 * 110 = HCLK / 8
	 * 111 = HCLK / 16
	 */

	if (ppre1 < 4U)
	{
		apb1_div = 1U;
	}
	else
	{
		apb1_div = 1U << (ppre1 - 3U);
	}

	/*
	 * Fréquence réellement reçue par USART2
	 */
	pclk1_freq = cpu_freq_hz / apb1_div;

	/* =========================================================
	 * 4. CALCUL AUTOMATIQUE DU BAUDRATE
	 * ========================================================= */

	/*
	 * USARTDIV = PCLK1 / (16 * Baudrate)
	 *
	 * Mais comme BRR contient directement :
	 *
	 * Mantisse * 16 + Fraction
	 *
	 * on peut calculer :
	 *
	 * USARTDIV_x16 = PCLK1 / Baudrate
	 *
	 * Le + baudrate/2 permet d'arrondir au plus proche.
	 */

	usartdiv_x16 = (pclk1_freq + (baudrate / 2U)) / baudrate;

	/*
	 * Partie entière
	 */
	mantissa = usartdiv_x16 / 16U;

	/*
	 * Partie fractionnaire
	 */
	fraction = usartdiv_x16 % 16U;

	/*
	 * BRR :
	 *
	 * [15:4] = mantisse
	 * [3:0]  = fraction
	 */
	USART2->BRR = (mantissa << 4) | fraction;

	/* ---------------------------------------------------------
	 * USART_CR2
	 * 1 bit de stop
	 * ---------------------------------------------------------
	 *
	 * STOP[1:0] = bits [13:12]
	 * 00 = 1 bit de stop
	 */

	USART2->CR2 &= ~(0x3U << 12);

	/* ---------------------------------------------------------
	 * USART_CR3
	 * Pas de contrôle de flux matériel RTS / CTS
	 * ---------------------------------------------------------
	 *
	 * CTSE = bit 9 = 0
	 * RTSE = bit 8 = 0
	 */

	USART2->CR3 &= ~((1U << 9) |
					 (1U << 8));

	/* ---------------------------------------------------------
	 * USART_CR1
	 * ---------------------------------------------------------
	 *
	 * UE  = 1 -> bit 13 : USART activé
	 * M   = 0 -> bit 12 : mot de 8 bits
	 * PCE = 0 -> bit 10 : pas de parité
	 * TE  = 1 -> bit 3  : transmission activée
	 * RE  = 1 -> bit 2  : réception activée
	 */

	/* On efface d'abord tous les champs que l'on veut configurer */
	USART2->CR1 &= ~((1U << 13) |
					 (1U << 12) |
					 (1U << 10) |
					 (1U << 3) |
					 (1U << 2));

	USART2->CR1 |= ((1U << 13) |
					(1U << 3) |
					(1U << 2));
}
/*
 * Envoie un caractère sur USART2
 */
void Serial_WriteChar(char c)
{
	/* Attendre que le registre de transmission soit vide */
	while (!(USART2->SR & USART_SR_TXE))
	{
	}

	/* Envoyer le caractère */
	USART2->DR = (uint8_t)c;
}

/*
 * Envoie une chaîne de caractères
 */
void Serial_WriteString(const char *str)
{
	while (*str != '\0')
	{
		Serial_WriteChar(*str);
		str++;
	}
}

/*
 * Fonction équivalente à printf()
 *
 * Exemple :
 *
 * Serial_Print("Bonjour\r\n");
 * Serial_Print("Valeur = %d\r\n", valeur);
 * Serial_Print("X=%d Y=%d Z=%d\r\n", x, y, z);
 */
void Serial_Print(const char *format, ...)
{
	char buffer[128];

	va_list args;

	/* Récupération des arguments après format */
	va_start(args, format);

	/* Transformation en chaîne de caractères */
	vsnprintf(buffer, sizeof(buffer), format, args);

	va_end(args);

	/* Envoi sur USART2 */
	Serial_WriteString(buffer);
}
/*
 * Equivalent de Serial.println()
 *
 * Ajoute automatiquement :
 *
 * \r = retour au début de la ligne
 * \n = passage à la ligne suivante
 */
void Serial_Println(const char *format, ...)
{
	char buffer[128];

	va_list args;

	va_start(args, format);

	vsnprintf(buffer, sizeof(buffer), format, args);

	va_end(args);

	Serial_WriteString(buffer);

	/*
	 * Retour au début de la ligne
	 */
	Serial_WriteChar('\r');

	/*
	 * Passage à la ligne suivante
	 */
	Serial_WriteChar('\n');
}

int main(void)
{
	SystemCoreClockUpdate();

    config_usart2(SystemCoreClock, 9600U);

    while (1)
    {
        Serial_Println("caca \n");
    }
}
