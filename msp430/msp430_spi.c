/* msp430_spi.c
 * Library for performing SPI I/O on a wide range of MSP430 chips.
 *
 * Serial interfaces supported:
 * 1. USI - developed on MSP430G2231
 * 2. USCI_A - developed on MSP430G2553
 * 3. USCI_B - developed on MSP430G2553
 * 4. USCI_A F5xxx - developed on MSP430F5172, added F5529
 * 5. USCI_B F5xxx - developed on MSP430F5172, added F5529
 *
 * Copyright (c) 2013, Eric Brundick <spirilis@linux.com>
 *
 * Permission to use, copy, modify, and/or distribute this software for any purpose
 * with or without fee is hereby granted, provided that the above copyright notice
 * and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES WITH
 * REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY AND
 * FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY SPECIAL, DIRECT, INDIRECT,
 * OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM LOSS OF USE,
 * DATA OR PROFITS, WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS
 * ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include <msp430.h>
#include "msp430_spi.h"


#ifdef __MSP430_HAS_USI__
void spi_init()
{
	/* USI SPI setup */
	USICTL0 |= USISWRST;
	USICTL1 = USICKPH;                // USICKPH=1 means sampling is done on the leading edge of the clock
	USICKCTL = USISSEL_2 | USIDIV_0;  // Clock source = SMCLK/1
	USICTL0 = USIPE7 | USIPE6 | USIPE5 | USIMST | USIOE;
	USISR = 0x0000;
}

uint8_t spi_transfer(uint8_t inb)
{
	USICTL1 |= USIIE;
	USISRL = inb;
	USICNT = 8;            // Start SPI transfer
	do {
		LPM0;                  // Light sleep while transferring
	} while (USICNT & 0x1F);
	USICTL1 &= ~USIIE;
	return USISRL;
}

/* What wonderful toys TI gives us!  A 16-bit SPI function. */
uint16_t spi_transfer16(uint16_t inw)
{
	USICTL1 |= USIIE;
	USISR = inw;
	USICNT = 16 | USI16B;  // Start 16-bit SPI transfer
	do {
		LPM0;                  // Light sleep while transferring
	} while (USICNT & 0x1F);
	USICTL1 &= ~USIIE;
	return USISR;
}

/* Not used by msprf24, but added for courtesy (LCD display support).  9-bit SPI. */
uint16_t spi_transfer9(uint16_t inw)
{
	USICTL1 |= USIIE;
	USISR = inw;
	USICNT = 9 | USI16B;  // Start 9-bit SPI transfer
	do {
		LPM0;                  // Light sleep while transferring
	} while (USICNT & 0x1F);
	USICTL1 &= ~USIIE;
	return USISR;
}
#endif

/* USCI 16-bit transfer functions rely on the Little-Endian architecture and use
 * an internal uint8_t * pointer to manipulate the individual 8-bit segments of a
 * 16-bit integer.
 */

// USCI for F2xxx and G2xx3 devices
#if defined(__MSP430_HAS_USCI__) && defined(SPI_DRIVER_USCI_A) && !defined(__MSP430_HAS_TB3__)
void spi_init()
{
	/* Configure ports on MSP430 device for USCI_A */
	P1SEL |= BIT1 | BIT2 | BIT4;
	P1SEL2 |= BIT1 | BIT2 | BIT4;

	/* USCI-A specific SPI setup */
	UCA0CTL1 |= UCSWRST;
	UCA0MCTL = 0x00;  // Clearing modulation control per TI user's guide recommendation
	UCA0CTL0 = UCCKPH | UCMSB | UCMST | UCMODE_0 | UCSYNC;  // SPI mode 0, master
	UCA0BR0 = 0x01;  // SPI clocked at same speed as SMCLK
	UCA0BR1 = 0x00;
	UCA0CTL1 = UCSSEL_2;  // Clock = SMCLK, clear UCSWRST and enables USCI_A module.
}

uint8_t spi_transfer(uint8_t inb)
{
	UCA0TXBUF = inb;
	while ( !(IFG2 & UCA0RXIFG) )  // Wait for RXIFG indicating remote byte received via SOMI
		;
	return UCA0RXBUF;
}

uint16_t spi_transfer16(uint16_t inw)
{
	uint16_t retw;
	uint8_t *retw8 = (uint8_t *)&retw, *inw8 = (uint8_t *)&inw;

	UCA0TXBUF = inw8[1];
	while ( !(IFG2 & UCA0RXIFG) )
		;
	retw8[1] = UCA0RXBUF;
	UCA0TXBUF = inw8[0];
	while ( !(IFG2 & UCA0RXIFG) )
		;
	retw8[0] = UCA0RXBUF;
	return retw;
}

uint16_t spi_transfer9(uint16_t inw)
{
	uint8_t p1dir_save, p1out_save, p1ren_save;
	uint16_t retw=0;

	/* Reconfigure I/O ports for bitbanging the MSB */
	p1ren_save = P1REN; p1out_save = P1OUT; p1dir_save = P1DIR;
	P1REN &= ~(BIT1 | BIT2 | BIT4);
	P1OUT &= ~(BIT1 | BIT2 | BIT4);
	P1DIR = (P1DIR & ~(BIT1 | BIT2 | BIT4)) | BIT2 | BIT4;
	P1SEL &= ~(BIT1 | BIT2 | BIT4);
	P1SEL2 &= ~(BIT1 | BIT2 | BIT4);

	// Perform single-bit transfer
	if (inw & 0x0100)
		P1OUT |= BIT2;
	P1OUT |= BIT4;
	if (P1IN & BIT1)
		retw |= 0x0100;
	P1OUT &= ~BIT4;

	// Restore port states and continue with 8-bit SPI
	P1SEL |= BIT1 | BIT2 | BIT4;
	P1SEL2 |= BIT1 | BIT2 | BIT4;
	P1DIR = p1dir_save;
	P1OUT = p1out_save;
	P1REN = p1ren_save;

	retw |= spi_transfer( (uint8_t)(inw & 0x00FF) );
	return retw;
}
#endif

#if defined(__MSP430_HAS_USCI__) && defined(SPI_DRIVER_USCI_B) && !defined(__MSP430_HAS_TB3__)
void spi_init()
{
	/* Configure ports on MSP430 device for USCI_B */
	P1SEL |= BIT5 | BIT6 | BIT7;
	P1SEL2 |= BIT5 | BIT6 | BIT7;

	/* USCI-B specific SPI setup */
	UCB0CTL1 |= UCSWRST;
	UCB0CTL0 = UCCKPH | UCMSB | UCMST | UCMODE_0 | UCSYNC;  // SPI mode 0, master
	UCB0BR0 = 0x01;  // SPI clocked at same speed as SMCLK
	UCB0BR1 = 0x00;
	UCB0CTL1 = UCSSEL_2;  // Clock = SMCLK, clear UCSWRST and enables USCI_B module.
}

uint8_t spi_transfer(uint8_t inb)
{
	UCB0TXBUF = inb;
	while ( !(IFG2 & UCB0RXIFG) )  // Wait for RXIFG indicating remote byte received via SOMI
		;
	return UCB0RXBUF;
}

uint16_t spi_transfer16(uint16_t inw)
{
	uint16_t retw;
	uint8_t *retw8 = (uint8_t *)&retw, *inw8 = (uint8_t *)&inw;

	UCB0TXBUF = inw8[1];
	while ( !(IFG2 & UCB0RXIFG) )
		;
	retw8[1] = UCB0RXBUF;
	UCB0TXBUF = inw8[0];
	while ( !(IFG2 & UCB0RXIFG) )
		;
	retw8[0] = UCB0RXBUF;
	return retw;
}

uint16_t spi_transfer9(uint16_t inw)
{
	uint8_t p1dir_save, p1out_save, p1ren_save;
	uint16_t retw=0;

	/* Reconfigure I/O ports for bitbanging the MSB */
	p1ren_save = P1REN; p1out_save = P1OUT; p1dir_save = P1DIR;
	P1REN &= ~(BIT5 | BIT6 | BIT7);
	P1OUT &= ~(BIT5 | BIT6 | BIT7);
	P1DIR = (P1DIR & ~(BIT5 | BIT6 | BIT7)) | BIT5 | BIT7;
	P1SEL &= ~(BIT5 | BIT6 | BIT7);
	P1SEL2 &= ~(BIT5 | BIT6 | BIT7);

	// Perform single-bit transfer
	if (inw & 0x0100)
		P1OUT |= BIT7;
	P1OUT |= BIT5;
	if (P1IN & BIT4)
		retw |= 0x0100;
	P1OUT &= ~BIT5;

	// Restore port states and continue with 8-bit SPI
	P1SEL |= BIT5 | BIT6 | BIT7;
	P1SEL2 |= BIT5 | BIT6 | BIT7;
	P1DIR = p1dir_save;
	P1OUT = p1out_save;
	P1REN = p1ren_save;

	retw |= spi_transfer( (uint8_t)(inw & 0x00FF) );
	return retw;
}
#endif

// USCI for G2xx4/G2xx5 devices
#if defined(__MSP430_HAS_USCI__) && defined(SPI_DRIVER_USCI_A) && defined(__MSP430_HAS_TB3__)
void spi_init()
{
	/* Configure ports on MSP430 device for USCI_A */
	P3SEL |= BIT0 | BIT4 | BIT5;
	P3SEL2 &= ~(BIT0 | BIT4 | BIT5);

	/* USCI-A specific SPI setup */
	UCA0CTL1 |= UCSWRST;
	UCA0MCTL = 0x00;  // Clearing modulation control per TI user's guide recommendation
	UCA0CTL0 = UCCKPH | UCMSB | UCMST | UCMODE_0 | UCSYNC;  // SPI mode 0, master
	UCA0BR0 = 0x01;  // SPI clocked at same speed as SMCLK
	UCA0BR1 = 0x00;
	UCA0CTL1 = UCSSEL_2;  // Clock = SMCLK, clear UCSWRST and enables USCI_A module.
}

uint8_t spi_transfer(uint8_t inb)
{
	UCA0TXBUF = inb;
	while ( !(IFG2 & UCA0RXIFG) )  // Wait for RXIFG indicating remote byte received via SOMI
		;
	return UCA0RXBUF;
}

uint16_t spi_transfer16(uint16_t inw)
{
	uint16_t retw;
	uint8_t *retw8 = (uint8_t *)&retw, *inw8 = (uint8_t *)&inw;

	UCA0TXBUF = inw8[1];
	while ( !(IFG2 & UCA0RXIFG) )
		;
	retw8[1] = UCA0RXBUF;
	UCA0TXBUF = inw8[0];
	while ( !(IFG2 & UCA0RXIFG) )
		;
	retw8[0] = UCA0RXBUF;
	return retw;
}

uint16_t spi_transfer9(uint16_t inw)
{
	uint8_t p3dir_save, p3out_save, p3ren_save;
	uint16_t retw=0;

	/* Reconfigure I/O ports for bitbanging the MSB */
	p3ren_save = P3REN; p3out_save = P3OUT; p3dir_save = P3DIR;
	P3REN &= ~(BIT0 | BIT4 | BIT5);
	P3OUT &= ~(BIT0 | BIT4 | BIT5);
	P3DIR = (P3DIR & ~(BIT0 | BIT4 | BIT5)) | BIT0 | BIT4;
	P3SEL &= ~(BIT0 | BIT4 | BIT5);
	P3SEL2 &= ~(BIT0 | BIT4 | BIT5);

	// Perform single-bit transfer
	if (inw & 0x0100)
		P3OUT |= BIT4;
	P3OUT |= BIT0;
	if (P3IN & BIT5)
		retw |= 0x0100;
	P3OUT &= ~BIT0;

	// Restore port states and continue with 8-bit SPI
	P3SEL |= BIT0 | BIT4 | BIT5;
	P3DIR = p3dir_save;
	P3OUT = p3out_save;
	P3REN = p3ren_save;

	retw |= spi_transfer( (uint8_t)(inw & 0x00FF) );
	return retw;
}
#endif

#if defined(__MSP430_HAS_USCI__) && defined(SPI_DRIVER_USCI_B) && defined(__MSP430_HAS_TB3__)
void spi_init()
{
	/* Configure ports on MSP430 device for USCI_B */
	P3SEL |= BIT1 | BIT2 | BIT3;
	P3SEL2 &= ~(BIT1 | BIT2 | BIT3);

	/* USCI-B specific SPI setup */
	UCB0CTL1 |= UCSWRST;
	UCB0CTL0 = UCCKPH | UCMSB | UCMST | UCMODE_0 | UCSYNC;  // SPI mode 0, master
	UCB0BR0 = 0x01;  // SPI clocked at same speed as SMCLK
	UCB0BR1 = 0x00;
	UCB0CTL1 = UCSSEL_2;  // Clock = SMCLK, clear UCSWRST and enables USCI_B module.
}

uint8_t spi_transfer(uint8_t inb)
{
	UCB0TXBUF = inb;
	while ( !(IFG2 & UCB0RXIFG) )  // Wait for RXIFG indicating remote byte received via SOMI
		;
	return UCB0RXBUF;
}

uint16_t spi_transfer16(uint16_t inw)
{
	uint16_t retw;
	uint8_t *retw8 = (uint8_t *)&retw, *inw8 = (uint8_t *)&inw;

	UCB0TXBUF = inw8[1];
	while ( !(IFG2 & UCB0RXIFG) )
		;
	retw8[1] = UCB0RXBUF;
	UCB0TXBUF = inw8[0];
	while ( !(IFG2 & UCB0RXIFG) )
		;
	retw8[0] = UCB0RXBUF;
	return retw;
}

uint16_t spi_transfer9(uint16_t inw)
{
	uint8_t p3dir_save, p3out_save, p3ren_save;
	uint16_t retw=0;

	/* Reconfigure I/O ports for bitbanging the MSB */
	p3ren_save = P3REN; p3out_save = P3OUT; p3dir_save = P3DIR;
	P3REN &= ~(BIT1 | BIT2 | BIT3);
	P3OUT &= ~(BIT1 | BIT2 | BIT3);
	P3DIR = (P3DIR & ~(BIT1 | BIT2 | BIT3)) | BIT1 | BIT3;
	P3SEL &= ~(BIT1 | BIT2 | BIT3);
	P3SEL2 &= ~(BIT1 | BIT2 | BIT3);

	// Perform single-bit transfer
	if (inw & 0x0100)
		P3OUT |= BIT1;
	P3OUT |= BIT3;
	if (P3IN & BIT2)
		retw |= 0x0100;
	P3OUT &= ~BIT3;

	// Restore port states and continue with 8-bit SPI
	P3SEL |= BIT1 | BIT2 | BIT3;
	P3DIR = p3dir_save;
	P3OUT = p3out_save;
	P3REN = p3ren_save;

	retw |= spi_transfer( (uint8_t)(inw & 0x00FF) );
	return retw;
}
#endif

// USCI for F5xxx/6xxx devices--F5172 specific P1SEL settings
#if defined(__MSP430_HAS_USCI_A0__) && defined(SPI_DRIVER_USCI_A)
void spi_init()
{
	/* Configure ports on MSP430 device for USCI_A */
	#ifdef __MSP430F5172
	P1SEL |= BIT0 | BIT1 | BIT2;
	#endif
	#ifdef __MSP430F5529
	P3SEL |= BIT3 | BIT4;
	P2SEL |= BIT7;
	#endif

	/* USCI-A specific SPI setup */
	UCA0CTL1 |= UCSWRST;
	UCA0MCTL = 0x00;  // Clearing modulation control per TI user's guide recommendation
	UCA0CTL0 = UCCKPH | UCMSB | UCMST | UCMODE_0 | UCSYNC;  // SPI mode 0, master
	UCA0BR0 = 0x01;  // SPI clocked at same speed as SMCLK
	UCA0BR1 = 0x00;
	UCA0CTL1 = UCSSEL_2;  // Clock = SMCLK, clear UCSWRST and enables USCI_A module.
}

uint8_t spi_transfer(uint8_t inb)
{
	UCA0TXBUF = inb;
	while ( !(UCA0IFG & UCRXIFG) )  // Wait for RXIFG indicating remote byte received via SOMI
		;
	return UCA0RXBUF;
}

uint16_t spi_transfer16(uint16_t inw)
{
	uint16_t retw;
	uint8_t *retw8 = (uint8_t *)&retw, *inw8 = (uint8_t *)&inw;

	UCA0TXBUF = inw8[1];
	while ( !(UCA0IFG & UCRXIFG) )
		;
	retw8[1] = UCA0RXBUF;
	UCA0TXBUF = inw8[0];
	while ( !(UCA0IFG & UCRXIFG) )
		;
	retw8[0] = UCA0RXBUF;
	return retw;
}

#ifdef __MS430F5172
uint16_t spi_transfer9(uint16_t inw)
{
	uint8_t p1dir_save, p1out_save, p1ren_save;
	uint16_t retw=0;

	/* Reconfigure I/O ports for bitbanging the MSB */
	p1ren_save = P1REN; p1out_save = P1OUT; p1dir_save = P1DIR;
	P1REN &= ~(BIT0 | BIT1 | BIT2);
	P1OUT &= ~(BIT0 | BIT1 | BIT2);
	P1DIR = (P1DIR & ~(BIT0 | BIT1 | BIT2)) | BIT0 | BIT1;
	P1SEL &= ~(BIT0 | BIT1 | BIT2);

	// Perform single-bit transfer
	if (inw & 0x0100)
		P1OUT |= BIT1;
	P1OUT |= BIT0;
	if (P1IN & BIT2)
		retw |= 0x0100;
	P1OUT &= ~BIT0;

	// Restore port states and continue with 8-bit SPI
	P1SEL |= BIT0 | BIT1 | BIT2;
	P1DIR = p1dir_save;
	P1OUT = p1out_save;
	P1REN = p1ren_save;

	retw |= spi_transfer( (uint8_t)(inw & 0x00FF) );
	return retw;
}
#endif

#ifdef __MS430F5529
uint16_t spi_transfer9(uint16_t inw)
{
	uint8_t p3dir_save, p3out_save, p3ren_save;
	uint8_t p2dir_save, p2out_save, p2ren_save;
	uint16_t retw=0;

	/* Reconfigure I/O ports for bitbanging the MSB */
	p3ren_save = P3REN; p3out_save = P3OUT; p3dir_save = P3DIR;
	p2ren_save = P2REN; p2out_save = P2OUT; p2dir_save = P2DIR;
	P3REN &= ~(BIT3 | BIT4); P2REN &= ~BIT7;
	P3OUT &= ~(BIT3 | BIT4); P2OUT &= ~BIT7;
	P3DIR = (P3DIR | ~(BIT3 | BIT4)) | BIT3; P2DIR |= BIT7;
	P3SEL &= ~(BIT3 | BIT4); P2SEL &= ~BIT7;

	// Perform single-bit transfer
	if (inw & 0x0100)
		P3OUT |= BIT3;
	P2OUT |= BIT7;
	if (P3IN & BIT4)
		retw |= 0x0100;
	P2OUT &= ~BIT7;

	// Restore port states and continue with 8-bit SPI
	P3SEL |= BIT3 | BIT4; P2SEL |= BIT7;
	P3DIR = p3dir_save; P2DIR = p2dir_save;
	P3OUT = p3out_save; P2OUT = p2out_save;
	P3REN = p3ren_save; P2REN = p2ren_save;

	retw |= spi_transfer( (uint8_t)(inw & 0x00FF) );
	return retw;
}
#endif

#endif

#if defined(__MSP430_HAS_USCI_B0__) && defined(SPI_DRIVER_USCI_B)
void spi_init()
{
	/* Configure ports on MSP430 device for USCI_B */
	#ifdef __MSP430F5172
	P1SEL |= BIT3 | BIT4 | BIT5;
	#endif
	#ifdef __MSP430F5529
	P3SEL |= BIT0 | BIT1 | BIT2;
	#endif

	/* USCI-B specific SPI setup */
	UCB0CTL1 |= UCSWRST;
	UCB0CTL0 = UCCKPH | UCMSB | UCMST | UCMODE_0 | UCSYNC;  // SPI mode 0, master
	UCB0BR0 = 0x01;  // SPI clocked at same speed as SMCLK
	UCB0BR1 = 0x00;
	UCB0CTL1 = UCSSEL_2;  // Clock = SMCLK, clear UCSWRST and enables USCI_B module.
}

uint8_t spi_transfer(uint8_t inb)
{
	UCB0TXBUF = inb;
	while ( !(UCB0IFG & UCRXIFG) )  // Wait for RXIFG indicating remote byte received via SOMI
		;
	return UCB0RXBUF;
}

uint16_t spi_transfer16(uint16_t inw)
{
	uint16_t retw;
	uint8_t *retw8 = (uint8_t *)&retw, *inw8 = (uint8_t *)&inw;

	UCB0TXBUF = inw8[1];
	while ( !(UCB0IFG & UCRXIFG) )
		;
	retw8[1] = UCB0RXBUF;
	UCB0TXBUF = inw8[0];
	while ( !(UCB0IFG & UCRXIFG) )
		;
	retw8[0] = UCB0RXBUF;
	return retw;
}

#ifdef __MSP430F5172
uint16_t spi_transfer9(uint16_t inw)
{
	uint8_t p1dir_save, p1out_save, p1ren_save;
	uint16_t retw=0;

	/* Reconfigure I/O ports for bitbanging the MSB */
	p1ren_save = P1REN; p1out_save = P1OUT; p1dir_save = P1DIR;
	P1REN &= ~(BIT3 | BIT4 | BIT5);
	P1OUT &= ~(BIT3 | BIT4 | BIT5);
	P1DIR = (P1DIR & ~(BIT3 | BIT4 | BIT5)) | BIT3 | BIT4;
	P1SEL &= ~(BIT3 | BIT4 | BIT5);

	// Perform single-bit transfer
	if (inw & 0x0100)
		P1OUT |= BIT4;
	P1OUT |= BIT3;
	if (P1IN & BIT5)
		retw |= 0x0100;
	P1OUT &= ~BIT3;

	// Restore port states and continue with 8-bit SPI
	P1SEL |= BIT3 | BIT4 | BIT5;
	P1DIR = p1dir_save;
	P1OUT = p1out_save;
	P1REN = p1ren_save;

	retw |= spi_transfer( (uint8_t)(inw & 0x00FF) );
	return retw;
}
#endif

#ifdef __MSP430F5529
uint16_t spi_transfer9(uint16_t inw)
{
	uint8_t p3dir_save, p3out_save, p3ren_save;
	uint16_t retw=0;

	/* Reconfigure I/O ports for bitbanging the MSB */
	p3ren_save = P3REN; p3out_save = P3OUT; p3dir_save = P3DIR;
	P3REN &= ~(BIT0 | BIT1 | BIT2);
	P3OUT &= ~(BIT0 | BIT1 | BIT2);
	P3DIR = (P3DIR & ~(BIT0 | BIT1 | BIT2)) | BIT0 | BIT2;
	P3SEL &= ~(BIT0 | BIT1 | BIT2);

	// Perform single-bit transfer
	if (inw & 0x0100)
		P3OUT |= BIT0;
	P3OUT |= BIT2;
	if (P3IN & BIT1)
		retw |= 0x0100;
	P3OUT &= ~BIT2;

	// Restore port states and continue with 8-bit SPI
	P3SEL |= BIT0 | BIT1 | BIT2;
	P3DIR = p3dir_save;
	P3OUT = p3out_save;
	P3REN = p3ren_save;

	retw |= spi_transfer( (uint8_t)(inw & 0x00FF) );
	return retw;
}
#endif

#endif
