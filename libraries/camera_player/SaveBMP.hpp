#include <stdint.h>
#include <stdio.h>
#include <string.h>
typedef uint8_t BYTE;
typedef uint16_t WORD;
typedef uint32_t DWORD;
typedef int32_t LONG;
typedef bool BOOLEAN;

#define BMP_ATTR_PACK __attribute__((packed)) __attribute__ ((aligned (2)))

typedef struct tagRGBQUAD
{
    BYTE    rgbBlue;
    BYTE    rgbGreen;
    BYTE    rgbRed;
    BYTE    rgbReserved;
} BMP_ATTR_PACK RGBQUAD;

typedef struct tagBITMAPINFOHEADER
{
    DWORD  biSize;
    LONG   biWidth;
    LONG   biHeight;
    WORD   biPlanes;
    WORD   biBitCount;
    DWORD  biCompression;
    DWORD  biSizeImage;
    LONG   biXPelsPerMeter;
    LONG   biYPelsPerMeter;
    DWORD  biClrUsed;
    DWORD  biClrImportant;
} BMP_ATTR_PACK BITMAPINFOHEADER, *PBITMAPINFOHEADER;

typedef struct tagBITMAPFILEHEADER
{
    WORD    bfType;
    DWORD   bfSize;
    WORD    bfReserved1;
    WORD    bfReserved2;
    DWORD   bfOffBits;
} BMP_ATTR_PACK BITMAPFILEHEADER, *PBITMAPFILEHEADER;

//-----------------------------------------------------------------------------
int SaveBMP( const std::string& filename, const char* pdata, int XSize, int YSize, int pitch, int bitsPerPixel )
//------------------------------------------------------------------------------
{
    static const WORD PALETTE_ENTRIES = 256;

    if( pdata )
    {
        FILE* pFile = fopen( filename.c_str(), "wb" );
        if( pFile )
        {
            BITMAPINFOHEADER    bih;
            BITMAPFILEHEADER    bfh;
            WORD                linelen = static_cast<WORD>( ( XSize * bitsPerPixel + 31 ) / 32 * 4 );  // DWORD aligned
            int                 YPos;
            int                 YStart = 0;

            memset( &bfh, 0, sizeof( BITMAPFILEHEADER ) );
            memset( &bih, 0, sizeof( BITMAPINFOHEADER ) );
            bfh.bfType          = 0x4d42;
            bfh.bfSize          = sizeof( bih ) + sizeof( bfh ) + sizeof( RGBQUAD ) * PALETTE_ENTRIES + static_cast<LONG>( linelen ) * static_cast<LONG>( YSize );
            bfh.bfOffBits       = sizeof( bih ) + sizeof( bfh ) + sizeof( RGBQUAD ) * PALETTE_ENTRIES;
            bih.biSize          = sizeof( bih );
            bih.biWidth         = XSize;
            bih.biHeight        = YSize;
            bih.biPlanes        = 1;
            bih.biBitCount      = static_cast<WORD>( bitsPerPixel );
            bih.biSizeImage     = static_cast<DWORD>( linelen ) * static_cast<DWORD>( YSize );

            if( ( fwrite( &bfh, sizeof( bfh ), 1, pFile ) == 1 ) && ( fwrite( &bih, sizeof( bih ), 1, pFile ) == 1 ) )
            {
                RGBQUAD rgbQ;
                for( int i = 0; i < PALETTE_ENTRIES; i++ )
                {
                    rgbQ.rgbRed      = static_cast<BYTE>( i );
                    rgbQ.rgbGreen    = static_cast<BYTE>( i );
                    rgbQ.rgbBlue     = static_cast<BYTE>( i );
                    rgbQ.rgbReserved = static_cast<BYTE>( 0 );
                    fwrite( &rgbQ, sizeof( rgbQ ), 1, pFile );
                }

                for( YPos = YStart + YSize - 1; YPos >= YStart; YPos-- )
                {
                    if( fwrite( &pdata[YPos * pitch], linelen, 1, pFile ) != 1 )
                    {
                        std::cout << "SaveBmp: ERR_WRITE_FILE: " << filename << std::endl;
                    }
                }
            }
            else
            {
                std::cout << "SaveBmp: ERR_WRITE_FILE: " << filename << std::endl;
            }
            fclose( pFile );
        }
        else
        {
            std::cout << "SaveBmp: ERR_CREATE_FILE: " << filename << std::endl;
        }
    }
    else
    {
        std::cout << "SaveBmp: ERR_DATA_INVALID:" << filename << std::endl;
    }
    return 0;
}
