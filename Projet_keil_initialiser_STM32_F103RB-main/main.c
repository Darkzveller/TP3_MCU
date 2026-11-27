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

/* TP3 : identification par badge DS2401 - STM32F103RB / CMSIS.
 * TIM3 remap partiel : PB4 = DQ (CH1), PB5 = espion (CH2), CH3 interne.
 * SWJ_CFG = 001 : libere seulement NJTRST (PB4), JTAG et SWD actifs.
 * DQ doit avoir une resistance de 3,3 kOhm (ou 4,7 kOhm) vers 3,3 V.
 * PA6/PA7 restent disponibles pour SPI1.
 */
#include <stdbool.h>

#define OC1(mode) ((uint32_t)(mode) << 4)
#define OC2(mode) ((uint32_t)(mode) << 12)
#define ACTIF_SUR_COMPARAISON   1U
#define INACTIF_SUR_COMPARAISON 2U
#define FORCE_INACTIF           4U
#define FORCE_ACTIF             5U

#define OW_RESET_D1 500U
#define OW_RESET_D2 570U
#define OW_RESET_D3 1020U
#define OW_WRITE1_D1 6U
#define OW_WRITE0_D1 65U
#define OW_READ_D1 3U
/* CH2 declenche avant 15 us pour laisser le temps d'entrer dans l'ISR. */
#define OW_READ_D2 11U
#define OW_SLOT_D3 80U
#define OW_READ_ROM 0x33U
#define OW_ROM_SIZE 8U

#define FLAG_Fin_Motif       (1U << 0)
#define FLAG_IButton_Present (1U << 1)
#define FLAG_Octet_Transmis  (1U << 2)
#define FLAG_Octet_Recu      (1U << 3)
#define FLAG_BADGE_VU        (1U << 4)

volatile uint8_t etat_one_wire = 1U;
volatile uint8_t motif_one_wire_fini = 1U;
volatile uint8_t NUM_Bit_Onewire = 0U;
volatile uint8_t Octet_data_Onewire = 0U;
volatile uint8_t Status_Onewire = 0U;
volatile uint8_t FLAG_BADGE = 0U;
static volatile uint32_t temps_ms = 0U;
static volatile uint8_t demande_badge = 0U;
static volatile uint8_t tempo_100ms = 0U;
static uint8_t badge_rom[OW_ROM_SIZE];
static volatile uint8_t index_rom = 0U;
static bool timer_ok = false;

/* Diagnostic : capture sans printf dans les interruptions.
 * Les tableaux contiennent les mesures du dernier octet lu. */
static volatile uint16_t diag_reset_cnt;
static volatile uint8_t diag_reset_sample;
static volatile uint8_t diag_reset_end;
static volatile uint16_t diag_read_cnt[8][8];
static volatile uint8_t diag_read_sample[8][8];
static volatile uint8_t diag_read_end[8][8];

/* Machine a etats executee uniquement dans l'interruption TIM3, sauf
 * l'amorcage d'une operation lorsque le timer est arrete. */
typedef enum {
    OW_IT_REPOS, OW_IT_RESET, OW_IT_ENVOI, OW_IT_RECEPTION
} OW_IT_State;
volatile OW_IT_State MAE_Onewire_It = OW_IT_REPOS;

typedef enum {
    BADGE_REPOS, BADGE_ATTENTE_RESET, BADGE_ATTENTE_ENVOI,
    BADGE_ATTENTE_RECEPTION
} OW_Badge_State;
OW_Badge_State MAE_Onewire_Badge = BADGE_REPOS;

/* ===== GPIO et LED : actives a 0 (cathodes vers GPIO) ===== */
static void LED_Rouge(uint8_t on)
{
    GPIOB->BSRR = on ? (1U << 16) : (1U << 0);
}

static void LED_Verte(uint8_t on)
{
    GPIOB->BSRR = on ? (1U << 17) : (1U << 1);
}

static void init_led(void)
{
    GPIO_InitPin(GPIOA, 5U, GPIO_OUTPUT_PP_2MHZ, GPIO_LOW);
    GPIO_InitPin(GPIOB, 0U, GPIO_OUTPUT_PP_2MHZ, GPIO_HIGH);
    GPIO_InitPin(GPIOB, 1U, GPIO_OUTPUT_PP_2MHZ, GPIO_HIGH);
}

static void LED_Carte(uint8_t on)
{
    GPIOA->BSRR = on ? (1U << 5) : (1U << 21);
}

void SysTick_Handler(void)
{
    temps_ms++;
    if (++tempo_100ms >= 100U) {
        tempo_100ms = 0U;
        demande_badge = 1U;
    }
}

static void delay_ms(uint32_t ms)
{
    uint32_t depart = temps_ms;
    while ((uint32_t)(temps_ms - depart) < ms) { }
}

/* Test execute une seule fois au demarrage, avant la lecture du badge. */
void Test_LED(void)
{
    LED_Rouge(0U);
    LED_Verte(0U);

    Serial_Println("Test LED rouge (PB0)");
    LED_Rouge(1U);
    delay_ms(500U);
    LED_Rouge(0U);
    delay_ms(200U);

    Serial_Println("Test LED verte (PB1)");
    LED_Verte(1U);
    delay_ms(500U);
    LED_Verte(0U);
    delay_ms(200U);

    Serial_Println("Fin du test LED");
}

/* ===== Partie 2.1 : broches OneWire ===== */
void init_pin_onewire(void)
{
    uint32_t map;
    RCC->APB2ENR |= RCC_APB2ENR_AFIOEN | RCC_APB2ENR_IOPBEN;
    (void)RCC->APB2ENR;

    /* TIM3_REMAP = 10 : CH1 PB4, CH2 PB5.
     * SWJ_CFG = 001 : JTAG et SWD actifs, seul NJTRST libere.
     * SWJ_CFG est en ecriture seule : effacer sa valeur relue.
     * Une seule ecriture MAPR, autres remappings conserves. */
    map = AFIO->MAPR;
    map &= ~((3U << 10) | (7U << 24));
    AFIO->MAPR = map | (2U << 10) | (1U << 24);

    GPIO_InitPin(GPIOB, 4U, GPIO_AF_OD_2MHZ, GPIO_HIGH);
    GPIO_InitPin(GPIOB, 5U, GPIO_AF_PP_2MHZ, GPIO_LOW);
}

/* ===== Partie 2.2 : configuration commune du timer ===== */
void init_timer(void)
{
    uint32_t ppre1 = (RCC->CFGR >> 8) & 7U;
    uint32_t tim3_hz = SystemCoreClock;
    if (ppre1 >= 4U) tim3_hz >>= (ppre1 - 4U);
    /* Ne pas produire de motifs hors specification si horloge incorrecte. */
    if (tim3_hz < 1000000U || tim3_hz % 1000000U != 0U ||
        tim3_hz / 1000000U > 65536U) return;

    RCC->APB1ENR |= RCC_APB1ENR_TIM3EN;
    (void)RCC->APB1ENR;
    RCC->APB1RSTR |= RCC_APB1RSTR_TIM3RST;
    RCC->APB1RSTR &= ~RCC_APB1RSTR_TIM3RST;
    TIM3->PSC = tim3_hz / 1000000U - 1U;
    TIM3->ARR = 0xFFFFU;
    TIM3->CCMR1 = OC1(FORCE_ACTIF) | OC2(FORCE_INACTIF);
    TIM3->CCMR2 = 0U; /* CH3 : comparaison interne, aucune broche. */
    TIM3->CCER = TIM_CCER_CC1E | TIM_CCER_CC2E;
    TIM3->EGR = TIM_EGR_UG;
    TIM3->SR = 0U;
    TIM3->DIER = 0U;
    NVIC_ClearPendingIRQ(TIM3_IRQn);
    NVIC_SetPriority(TIM3_IRQn, 0U);
    NVIC_EnableIRQ(TIM3_IRQn);
    timer_ok = true;
}

/* D1 : liberation DQ, D2 : lecture, D3 : fin du motif (en us).
 * Pour les motifs d'ecriture D2 reste valide, la lecture est ignoree.
 * Appel uniquement quand TIM3 est arrete. */
void init_motif(uint16_t D1, uint16_t D2, uint16_t D3)
{
    uint32_t masque_it;
    if (!timer_ok || !D1 || !D2 || D1 >= D3 || D2 >= D3) return;
    masque_it = __get_PRIMASK();
    __disable_irq();
    TIM3->CR1 &= ~TIM_CR1_CEN;
    TIM3->DIER = 0U;
    TIM3->CCR1 = D1;
    TIM3->CCR2 = D2;
    TIM3->CCR3 = D3;
    TIM3->EGR = TIM_EGR_UG;
    TIM3->SR = 0U;
    motif_one_wire_fini = 0U;
    Status_Onewire &= (uint8_t)~FLAG_Fin_Motif;
    TIM3->DIER = TIM_DIER_CC2IE | TIM_DIER_CC3IE;
    /* Au premier tick CNT passe de 0xffff a 0. Les deux fronts de DQ
     * sont ainsi produits par le materiel, meme si le CPU est interrompu.
     * CH1 PWM2 : bas si CNT < D1, relache ensuite.
     * CH2 PWM1 : haut si CNT < D2, bas ensuite. */
    TIM3->CNT = 0xFFFFU;
    TIM3->CCMR1 = OC1(7U) | OC2(6U);
    TIM3->CR1 |= TIM_CR1_CEN;
    __set_PRIMASK(masque_it);
}

static void motif_reset(void) { init_motif(OW_RESET_D1, OW_RESET_D2, OW_RESET_D3); }
static void motif_ecriture(uint8_t bit)
{
    init_motif(bit ? OW_WRITE1_D1 : OW_WRITE0_D1, OW_READ_D2, OW_SLOT_D3);
}
static void motif_lecture(void) { init_motif(OW_READ_D1, OW_READ_D2, OW_SLOT_D3); }

/* ===== Parties 2.3 et 3 : interruptions CH2/CH3 ===== */
void TIM3_IRQHandler(void)
{
    uint16_t sample_time;
    if ((TIM3->SR & TIM_SR_CC2IF) && (TIM3->DIER & TIM_DIER_CC2IE)) {
        /* Lire le niveau physique du bus OneWire sur PB4. */
        etat_one_wire = (uint8_t)((GPIOB->IDR >> 4) & 1U);
        sample_time = (uint16_t)TIM3->CNT;
        /* CNT est lu juste APRES l'echantillonnage de PB4. */
        if (MAE_Onewire_It == OW_IT_RESET) {
            diag_reset_cnt = sample_time;
            diag_reset_sample = etat_one_wire;
        } else if (MAE_Onewire_It == OW_IT_RECEPTION && NUM_Bit_Onewire < 8U) {
            diag_read_cnt[index_rom][NUM_Bit_Onewire] = sample_time;
            diag_read_sample[index_rom][NUM_Bit_Onewire] = etat_one_wire;
        }
        TIM3->SR = (uint16_t)~TIM_SR_CC2IF;
    }
    if ((TIM3->SR & TIM_SR_CC3IF) && (TIM3->DIER & TIM_DIER_CC3IE)) {
        TIM3->CR1 &= ~TIM_CR1_CEN;
        TIM3->DIER = 0U;
        TIM3->SR = (uint16_t)~TIM_SR_CC3IF;
        /* Mesurer DQ AVANT de forcer le retour au repos. */
        if (MAE_Onewire_It == OW_IT_RESET) {
            diag_reset_end = (uint8_t)((GPIOB->IDR >> 4) & 1U);
        } else if (MAE_Onewire_It == OW_IT_RECEPTION && NUM_Bit_Onewire < 8U) {
            diag_read_end[index_rom][NUM_Bit_Onewire] = (uint8_t)((GPIOB->IDR >> 4) & 1U);
        }
        /* Garantir le repos meme lors d'un changement d'etat. */
        TIM3->CCMR1 = OC1(FORCE_ACTIF) | OC2(FORCE_INACTIF);
        motif_one_wire_fini = 1U;
        Status_Onewire |= FLAG_Fin_Motif;

        switch (MAE_Onewire_It) {
        case OW_IT_RESET:
            if (etat_one_wire == 0U && diag_reset_end == 1U) Status_Onewire |= FLAG_IButton_Present;
            MAE_Onewire_It = OW_IT_REPOS;
            break;
        case OW_IT_ENVOI:
            if (++NUM_Bit_Onewire < 8U) {
                motif_ecriture((uint8_t)((Octet_data_Onewire >> NUM_Bit_Onewire) & 1U));
            } else {
                MAE_Onewire_It = OW_IT_REPOS;
                Status_Onewire |= FLAG_Octet_Transmis;
            }
            break;
        case OW_IT_RECEPTION:
            if (etat_one_wire) Octet_data_Onewire |= (uint8_t)(1U << NUM_Bit_Onewire);
            if (++NUM_Bit_Onewire < 8U) {
                motif_lecture();
            } else {
                MAE_Onewire_It = OW_IT_REPOS;
                Status_Onewire |= FLAG_Octet_Recu;
            }
            break;
        default:
            break; /* Aucune operation en cours. */
        }
    }
}

/* CRC Dallas / Maxim : x^8 + x^5 + x^4 + 1 (forme reflechie 0x8C). */
uint8_t ONEWIRE_CRC8(const uint8_t *data, uint8_t taille)
{
    uint8_t crc = 0U, i, j, octet, mix;
    for (i = 0U; i < taille; i++) {
        octet = data[i];
        for (j = 0U; j < 8U; j++) {
            mix = (uint8_t)((crc ^ octet) & 1U);
            crc >>= 1;
            if (mix) crc ^= 0x8CU;
            octet >>= 1;
        }
    }
    return crc;
}
static bool ROM_valide(const uint8_t rom[OW_ROM_SIZE])
{
    return rom[0] == 0x01U && ONEWIRE_CRC8(rom, 7U) == rom[7];
}

/* ===== Partie 3 : lecture non bloquante, machine a etats dans le main =====
 * Les amorcages suivants ne sont executes que lorsque l'IT a fini.
 * Le TIM3 ne tourne pas pendant les modifications des variables partagees.
 */
static void RESET_ONEWIRE_NB(void)
{
    Status_Onewire = 0U;
    MAE_Onewire_It = OW_IT_RESET;
    motif_reset();
}
static void ENVOI_OCTET_ONEWIRE_NB(uint8_t octet)
{
    Status_Onewire &= (uint8_t)~FLAG_Octet_Transmis;
    NUM_Bit_Onewire = 0U;
    Octet_data_Onewire = octet;
    MAE_Onewire_It = OW_IT_ENVOI;
    motif_ecriture(octet & 1U);
}
static void LECTURE_OCTET_ONEWIRE_NB(void)
{
    Status_Onewire &= (uint8_t)~FLAG_Octet_Recu;
    NUM_Bit_Onewire = 0U;
    Octet_data_Onewire = 0U;
    MAE_Onewire_It = OW_IT_RECEPTION;
    motif_lecture();
}

static bool prendre_demande_badge(void)
{
    uint32_t masque = __get_PRIMASK();
    bool demande;
    __disable_irq();
    demande = demande_badge != 0U;
    demande_badge = 0U;
    __set_PRIMASK(masque);
    return demande;
}

/* Les captures sont imprimees dans le main apres la transaction.
 * Un groupe de huit caracteres correspond a b0 ... b7 sur le fil. */
static uint32_t tentative;
static uint32_t debut_transaction;
static uint32_t dernier_rapport;
static void erreur_bus(const char *message)
{
    LED_Carte(0U);
    LED_Verte(0U);
    LED_Rouge(1U);
    Serial_Println("[ERREUR] %s", message);
}

static void rapport_rom(void)
{
    uint8_t i, bit;
    bool fin_basse = false, lecture_tardive = false;
    for (i = 0; i < OW_ROM_SIZE; ++i) {
        char bits[9], fins[9];
        uint16_t min = 65535U, max = 0;
        for (bit = 0; bit < 8U; ++bit) {
            uint16_t t = diag_read_cnt[i][bit];
            bits[bit] = diag_read_sample[i][bit] ? '1' : '0';
            fins[bit] = diag_read_end[i][bit] ? '1' : '0';
            if (fins[bit] == '0') fin_basse = true;
            if (t > 15U) lecture_tardive = true;
            if (t < min) min = t;
            if (t > max) max = t;
        }
        bits[8] = fins[8] = 0;
        Serial_Println("[RX %u/8] %02X  bits b0..b7=%s  DQ fin=%s  CNT=%u..%u us",
            (unsigned)i+1U, (unsigned)badge_rom[i], bits, fins, (unsigned)min, (unsigned)max);
    }
    Serial_Println("[CRC] famille=%02X (attendu 01), calcule=%02X, recu=%02X",
        (unsigned)badge_rom[0], (unsigned)ONEWIRE_CRC8(badge_rom,7), (unsigned)badge_rom[7]);
    if (fin_basse) erreur_bus("DQ encore bas en fin de creneau");
    else if (lecture_tardive) erreur_bus("Echantillonnage possiblement tardif (>15 us), voir CNT");
    else if (!ROM_valide(badge_rom)) erreur_bus("Famille ou CRC incorrect");
    else {
        FLAG_BADGE |= FLAG_BADGE_VU;
        LED_Carte(1U); LED_Rouge(0U); LED_Verte(1U);
    }
}

void Gestion_Badge(void)
{
    if (MAE_Onewire_Badge != BADGE_REPOS && (uint32_t)(temps_ms-debut_transaction) > 50U) {
        __disable_irq();
        TIM3->DIER = 0U; TIM3->CR1 &= ~TIM_CR1_CEN;
        TIM3->CCMR1 = OC1(FORCE_ACTIF) | OC2(FORCE_INACTIF);
        TIM3->SR = 0U;
        NVIC_ClearPendingIRQ(TIM3_IRQn);
        MAE_Onewire_It = OW_IT_REPOS;
        __enable_irq();
        erreur_bus("Delai de transaction depasse, verifier TIM3/IRQ");
        MAE_Onewire_Badge = BADGE_REPOS;
    }
    switch (MAE_Onewire_Badge) {
    case BADGE_REPOS:
        if (!prendre_demande_badge() || (uint32_t)(temps_ms-dernier_rapport) < 1000U) break;
        dernier_rapport = temps_ms;
        Serial_Println("[ESSAI %lu] PB4 au repos=%u", (unsigned long)++tentative,
                       (unsigned)((GPIOB->IDR >> 4) & 1U));
        if (!(GPIOB->IDR & (1U << 4))) {
            erreur_bus("DQ bas avant reset. Verifier pull-up 4.7k vers 3V3, masse et contact.");
            break;
        }
        debut_transaction = temps_ms;
        MAE_Onewire_Badge = BADGE_ATTENTE_RESET;
        RESET_ONEWIRE_NB();
        break;
    case BADGE_ATTENTE_RESET:
        if (!(Status_Onewire & FLAG_Fin_Motif)) break;
        if (Status_Onewire & FLAG_IButton_Present) {
            MAE_Onewire_Badge = BADGE_ATTENTE_ENVOI;
            ENVOI_OCTET_ONEWIRE_NB(OW_READ_ROM);
            break;
        }
        Serial_Println("[RESET] CNT=%u us, DQ lecture=%u, DQ fin=%u",
              (unsigned)diag_reset_cnt, (unsigned)diag_reset_sample, (unsigned)diag_reset_end);
        if (!diag_reset_end) erreur_bus("DQ reste bas apres reset. Reessayer badge debranche.");
        else {
            LED_Carte(0U); LED_Rouge(0U); LED_Verte(0U);
            Serial_Println("[ABSENT] Aucune presence detectee.");
        }
        MAE_Onewire_Badge = BADGE_REPOS;
        break;
    case BADGE_ATTENTE_ENVOI:
        if (!(Status_Onewire & FLAG_Octet_Transmis)) break;
        index_rom = 0U;
        MAE_Onewire_Badge = BADGE_ATTENTE_RECEPTION;
        LECTURE_OCTET_ONEWIRE_NB();
        break;
    case BADGE_ATTENTE_RECEPTION:
        if (!(Status_Onewire & FLAG_Octet_Recu)) break;
        badge_rom[index_rom++] = Octet_data_Onewire;
        if (index_rom < OW_ROM_SIZE) LECTURE_OCTET_ONEWIRE_NB();
        else {
            Serial_Println("[RESET] Presence, CNT=%u us, DQ lecture=0, DQ fin=1", (unsigned)diag_reset_cnt);
            Serial_Println("[TX] READ ROM 0x33 envoye, bit faible en premier");
            rapport_rom();
            MAE_Onewire_Badge = BADGE_REPOS;
        }
        break;
    }
}

/* ===== Programme principal : version finale du TP3 ===== */
int main(void)
{
    uint32_t masque;

    SystemCoreClockUpdate();
    config_usart2(SystemCoreClock, 9600U);
    init_led();

    /* SysTick a 1 ms : temporisations et interrogation a 10 Hz. */
    if (SysTick_Config(SystemCoreClock / 1000U) != 0U) {
        Serial_Println("Erreur SysTick");
        LED_Rouge(1U);
        while (1) { }
    }
    NVIC_SetPriority(SysTick_IRQn, 3U);

    Test_LED();

    /* Initialiser le timer avant de connecter ses sorties aux GPIO. */
    init_timer();
    if (!timer_ok) {
        Serial_Println("Erreur horloge TIM3");
        LED_Rouge(1U);
        while (1) { }
    }
    init_pin_onewire();
    delay_ms(2U);
    Serial_Println("[INIT] USART2 9600 8N1, HCLK=%lu Hz, TIM3 PSC=%u, tick=1 us", (unsigned long)SystemCoreClock, (unsigned)TIM3->PSC);
    Serial_Println("[INIT] PB4=DQ drain ouvert, PB5=espion. Reset bas 500 us, lecture 570 us, fin 1020 us.");
    Serial_Println("Etat PB4 au repos : %u", (unsigned)((GPIOB->IDR >> 4) & 1U));
    Serial_Println("TP3 : attente du badge DS2401");

    /* Repartir sur une periode complete apres le test LED. */
    masque = __get_PRIMASK();
    __disable_irq();
    demande_badge = 0U;
    tempo_100ms = 0U;
    __set_PRIMASK(masque);

    while (1) {
        Gestion_Badge();

        if (FLAG_BADGE & FLAG_BADGE_VU) {
            Serial_Println("[OK DS2401] ROM: %02X %02X %02X %02X %02X %02X %02X %02X",
                (unsigned)badge_rom[0], (unsigned)badge_rom[1],
                (unsigned)badge_rom[2], (unsigned)badge_rom[3],
                (unsigned)badge_rom[4], (unsigned)badge_rom[5],
                (unsigned)badge_rom[6], (unsigned)badge_rom[7]);

            FLAG_BADGE &= (uint8_t)~FLAG_BADGE_VU;
        }
    }
}
