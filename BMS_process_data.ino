bool isPacketValid(byte *packet) //check if packet is valid
{
    TRACE;
    if (packet == nullptr)
    {
        return false;
    }

    bmsPacketHeaderStruct *pHeader = (bmsPacketHeaderStruct *)packet;
    int checksumLen = pHeader->dataLen + 2; // status + data len + data

    if (pHeader->start != 0xDD)
    {
        return false;
    }

    int offset = 2; // header 0xDD and command type are skipped

    byte checksum = 0;
    for (int i = 0; i < checksumLen; i++)
    {
        checksum += packet[offset + i];
    }

    //printf("checksum: %x\n", checksum);

    checksum = ((checksum ^ 0xFF) + 1) & 0xFF;
    //printf("checksum v2: %x\n", checksum);

    byte rxChecksum = packet[offset + checksumLen + 1];

    if (checksum == rxChecksum)
    {
        //printf("Packet is valid\n");
        return true;
    }
    else
    {
        //printf("Packet is not valid\n");
        //printf("Expected value: %x\n", rxChecksum);
        return false;
    }
}

bool processBasicInfo(packBasicInfoStruct *output, byte *data, unsigned int dataLen)
{
    TRACE;
    // Expected data len
    if (dataLen != 0x1B)
    {
        return false;
    }

    output->Volts = ((float)two_ints_into16(data[0], data[1])) / 100; // Resolution 10 mV -> convert to volts
    output->Amps = ((float)two_ints_into16(data[2], data[3])) / 100;  // Resolution 10 mA -> convert to amps
    output->CapacityRemainAh = ((float)two_ints_into16(data[4], data[5])) / 100;
    output->CapacityRemainPercent = ((int)data[19]);
    output->Temp1 = (((float)two_ints_into16(data[23], data[24])) - 2731) / 10;
    output->Temp2 = (((float)two_ints_into16(data[25], data[26])) - 2731) / 10;
    output->BalanceCodeLow = (two_ints_into16(data[12], data[13]));
    output->BalanceCodeHigh = (two_ints_into16(data[14], data[15]));
    output->MosfetStatus = ((byte)data[20]);

    return true;
};

bool processCellInfo(packCellInfoStruct *output, byte *data, unsigned int dataLen)
{
    TRACE;
    float _cellSum;
    float _cellMin = 50.0;
    float _cellMax = 0;
    float _cellAvg;
    float _cellDiff;

    output->NumOfCells = dataLen / 2; // Data length * 2 is number of cells !!!!!!

    //go trough individual cells
    for (byte i = 0; i < dataLen / 2; i++)
    {
        output->CellVolt[i] = ((float)two_ints_into16(data[i * 2], data[i * 2 + 1])) / 1000; // Resolution 1 mV -> convert to volts
        _cellSum += output->CellVolt[i];
        if (output->CellVolt[i] > _cellMax)
        {
            _cellMax = output->CellVolt[i];
        }
        if (output->CellVolt[i] < _cellMin)
        {
            _cellMin = output->CellVolt[i];
        }

        output->CellColor[i] = getPixelColorHsv(mapHueFloat(output->CellVolt[i], c_cellAbsMin, c_cellAbsMax), 255, 255);

        //getPixelColorHsv(mapHue(mySpectrum, 0, 100), c_sat, c_val))
    }
    output->CellMin = _cellMin;
    output->CellMax = _cellMax;
    output->CellDiff = _cellMax - _cellMin; // Resolution 10 mV -> convert to volts
    output->CellAvg = _cellSum / output->NumOfCells;

    return true;
};

bool bmsProcessPacket(byte *packet)
{
    TRACE;
    bool isValid = isPacketValid(packet);

    if (isValid != true)
    {
        commSerial.println("Invalid packer received");
        return false;
    }

    bmsPacketHeaderStruct *pHeader = (bmsPacketHeaderStruct *)packet;
    byte *data = packet + sizeof(bmsPacketHeaderStruct); // TODO Fix this ugly hack
    unsigned int dataLen = pHeader->dataLen;

    bool result = false;

    // |Decision based on pac ket type (info3 or info4)
    switch (pHeader->type)
    {
    case cBasicInfo3:
    {
        // Process basic info
        result = processBasicInfo(&packBasicInfo, data, dataLen);
        //showBasicInfo();
        newPacketReceived = true;
        break;
    }

    case cCellInfo4:
    {
        result = processCellInfo(&packCellInfo, data, dataLen);
        //showCellInfo();
        newPacketReceived = true;
        break;
    }

    default:
        result = false;
        commSerial.printf("Unsupported packet type detected. Type: %d", pHeader->type);
    }

    return result;
}

bool bmsCollectPacket_uart(byte *packet) //unused function to get packet directly from uart
{
    TRACE;
#define packet1stByte 0xdd
#define packet2ndByte 0x03
#define packet2ndByte_alt 0x04
#define packetLastByte 0x77
    bool retVal;
    byte actualByte;
    static byte previousByte;
    static bool inProgress = false;
    static byte bmsPacketBuff[40];
    static byte bmsPacketBuffCount;

    if (bmsSerial.available() > 0) //data in serial buffer available
    {
        actualByte = bmsSerial.read();
        if (previousByte == packetLastByte && actualByte == packet1stByte) //got packet footer
        {
            //commSerial.println("");
            //commSerial.print("got packet footer. packet length = ");
            //commSerial.println(bmsPacketBuffCount);

            memcpy(packet, bmsPacketBuff, bmsPacketBuffCount);

            //for (int i=0; i < bmsPacketBuffCount; i++)  //spit out resulting packet byte by byte here
            //{
            //commSerial.print(bmsPacketBuff[i], HEX);
            //commSerial.print(" ");
            //}
            //commSerial.println("");
            inProgress = false;
            retVal = true;
        }
        if (inProgress) //filling bytes to output buffer
        {
            bmsPacketBuff[bmsPacketBuffCount] = actualByte;
            bmsPacketBuffCount++;
            retVal = false;
            //commSerial.print(".");
        }

        if (previousByte == packet1stByte && (actualByte == packet2ndByte || actualByte == packet2ndByte_alt)) //got packet header
        {
            //commSerial.println("");
            //commSerial.println("");
            //commSerial.print("got packet header. accumulating data.");
            bmsPacketBuff[0] = previousByte;
            bmsPacketBuff[1] = actualByte;
            bmsPacketBuffCount = 2; // for next pass. [0] and [1] are filled already
            inProgress = true;
            retVal = false;
        }

        previousByte = actualByte;
    }
    else
    {
        retVal = false;
    }
    return retVal;
}

bool bleCollectPacket(char *data, uint32_t dataSize) // reconstruct packet from BLE incomming data, called by notifyCallback function
{
    TRACE;
    static uint8_t packetstate = 0; //0 - empty, 1 - first half of packet received, 2- second half of packet received
    static uint8_t packetbuff[40] = {0x0};
    static uint32_t previousDataSize = 0;
    bool retVal = false;
    //hexDump(data,dataSize);

    if (data[0] == 0xdd && packetstate == 0) // probably got 1st half of packet
    {
        packetstate = 1;
        previousDataSize = dataSize;
        for (uint8_t i = 0; i < dataSize; i++)
        {
            packetbuff[i] = data[i];
        }
        retVal = false;
    }

    if (data[dataSize - 1] == 0x77 && packetstate == 1) //probably got 2nd half of the packet
    {
        packetstate = 2;
        for (uint8_t i = 0; i < dataSize; i++)
        {
            packetbuff[i + previousDataSize] = data[i];
        }
        retVal = false;
    }

    if (packetstate == 2) //got full packet
    {
        uint8_t packet[dataSize + previousDataSize];
        memcpy(packet, packetbuff, dataSize + previousDataSize);

        bmsProcessPacket(packet); //pass pointer to retrieved packet to processing function
        packetstate = 0;
        retVal = true;
    }
    return retVal;
}
void bmsGetInfo3()
{
    TRACE;
    // header status command length data checksum footer
    //   DD     A5      03     00    FF     FD      77
    uint8_t data[7] = {0xdd, 0xa5, 0x3, 0x0, 0xff, 0xfd, 0x77};
    //bmsSerial.write(data, 7);
    sendCommand(data, sizeof(data));
    //commSerial.println("Request info3 sent");
}

void bmsGetInfo4()
{
    TRACE;
    //  DD  A5 04 00  FF  FC  77
    uint8_t data[7] = {0xdd, 0xa5, 0x4, 0x0, 0xff, 0xfc, 0x77};
    //bmsSerial.write(data, 7);
    sendCommand(data, sizeof(data));
    //commSerial.println("Request info4 sent");
}

void showBasicInfo() //debug all data to uart
{
    TRACE;
    commSerial.printf("Total voltage: %f\n", packBasicInfo.Volts);
    commSerial.printf("Amps: %f\n", packBasicInfo.Amps);
    commSerial.printf("CapacityRemainAh: %f\n", packBasicInfo.CapacityRemainAh);
    commSerial.printf("CapacityRemainPercent: %d\n", packBasicInfo.CapacityRemainPercent);
    commSerial.printf("Temp1: %f\n", packBasicInfo.Temp1);
    commSerial.printf("Temp2: %f\n", packBasicInfo.Temp2);
    commSerial.printf("Balance Code Low: 0x%x\n", packBasicInfo.BalanceCodeLow);
    commSerial.printf("Balance Code High: 0x%x\n", packBasicInfo.BalanceCodeHigh);
    commSerial.printf("Mosfet Status: 0x%x\n", packBasicInfo.MosfetStatus);
}

void showCellInfo() //debug all data to uart
{
    TRACE;
    commSerial.printf("Number of cells: %u\n", packCellInfo.NumOfCells);
    for (byte i = 1; i <= packCellInfo.NumOfCells; i++)
    {
        commSerial.printf("Cell no. %u", i);
        commSerial.printf("   %f\n", packCellInfo.CellVolt[i - 1]);
    }
    commSerial.printf("Max cell volt: %f\n", packCellInfo.CellMax);
    commSerial.printf("Min cell volt: %f\n", packCellInfo.CellMin);
    commSerial.printf("Difference cell volt: %f\n", packCellInfo.CellDiff);
    commSerial.printf("Average cell volt: %f\n", packCellInfo.CellAvg);
    commSerial.println();
}

void hexDump(const char *data, uint32_t dataSize) //debug function
{
    TRACE;
    commSerial.println("HEX data:");

    for (int i = 0; i < dataSize; i++)
    {
        commSerial.printf("0x%x, ", data[i]);
    }
    commSerial.println("");
}

int16_t two_ints_into16(int highbyte, int lowbyte) // turns two bytes into a single long integer
{
    TRACE;
    int16_t result = (highbyte);
    result <<= 8;                //Left shift 8 bits,
    result = (result | lowbyte); //OR operation, merge the two
    return result;
}
