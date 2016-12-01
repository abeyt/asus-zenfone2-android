/*
 *  max17058_battery.c
 *  fuel-gauge systems for lithium-ion (Li+) batteries
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/platform_device.h>
#include <linux/mutex.h>
#include <linux/err.h>
#include <linux/i2c.h>
#include <linux/delay.h>
#include <linux/power_supply.h>
#include <linux/slab.h>
//#include <linux/qpnp/qpnp-adc.h>
#include <linux/HWVersion.h>
#include <linux/switch.h>

#include "asus_battery.h"

#define max17058_VCELL_REG			0x02/*max17058_VCELL_MSB*/
#define max17058_SOC_REG				0x04/*max17058_SOC_MSB*/
#define max17058_MODE_REG			0x06/*max17058_MODE_MSB*/
#define max17058_VER_REG				0x08/*max17058_VER_MSB*/
#define max17058_RCOMP_REG			0x0C/*max17058_RCOMP_MSB*/
#define max17058_VRESET_REG			0x18
#define max17058_STATUS_REG			0x1A
#define max17058_CMD_REG				0xFE/*max17058_CMD_MSB*/	
#define max17058_MODEL_ACCESS_REG	0x3E
#define max17058_OCV_REG				0x0E	//add by peter
#define MAX17058_TABLE				0x40	//add by peter

#define max17058_MODEL_ACCESS_UNLOCK		0x4A57
#define max17058_MODEL_ACCESS_LOCK		0x0000
#define max17058_POR_CMD					0x5400 //add by peter

#define max17058_DELAY		10*HZ //1000->10*HZ
#define max17058_BATTERY_FULL	95

//below are from .ini file
//--------------------.ini file---------------------------------
#define INI_RCOMP 		(127)
#define INI_RCOMP_FACTOR	1
const static int TempCoHot = -0.7*INI_RCOMP_FACTOR;
const static int TempCoCold = -5.175*INI_RCOMP_FACTOR;

#define INI_SOCCHECKA		(228)
#define INI_SOCCHECKB		(230)
#define INI_OCVTEST 		(57408) 
#define INI_BITS		(19)
//--------------------.ini file end-------------------------------

#define VERIFY_AND_FIX 1
#define LOAD_MODEL !(VERIFY_AND_FIX)
#define GAUGE_ERR(...)        printk(KERN_ERR "[MAX17058_ERR] " __VA_ARGS__);
#define GAUGE_INFO(...)      printk(KERN_INFO "[MAX17058] " __VA_ARGS__)

extern int Read_PROJ_ID(void);

static int max17058_check_por(struct i2c_client *client);
static void prepare_to_load_model(struct i2c_client *client);
static void load_model(struct i2c_client *client);
static bool verify_model_is_correct(struct i2c_client *client);
static void cleanup_model_load(struct i2c_client *client);
static int max17058_write_reg(struct i2c_client *client, u8 reg, u16 value);
static int max17058_read_reg(struct i2c_client *client, u8 reg);
static u8 original_OCV_1=0, original_OCV_2=0;
static u8 por_flag = 0;
static struct dev_func max17058_tbl;
struct switch_dev max17058_batt_dev;
static struct max17058_chip *g_max17058_chip;

struct max17058_chip {
	struct i2c_client		*client;
	struct delayed_work		work;
	struct delayed_work		hand_work;
#ifdef MAX17058_REG_POWERSUPPLY
	struct power_supply		fgbattery;
	struct power_supply		*bms_psy;
#endif
	/* State Of Connect */
	int online;
	/* battery voltage */
	int vcell;
	/* battery capacity */
	int soc;
	/* State Of Charge */
	int status;
};

#if 1
	uint8_t model_data[] = {
		0xAA, 0x00, 0xB6, 0x40, 0xB7, 0xA0, 0xB9, 0xC0,
		0xBB, 0xE0, 0xBC, 0xD0, 0xBD, 0x40, 0xBD, 0xA0,
		0xC0, 0x30, 0xC0, 0xF0, 0xC4, 0x80, 0xC6, 0x00,
		0xC8, 0x80, 0xCB, 0x00, 0xCF, 0xE0, 0xD6, 0x40,
		0x01, 0xC0, 0x17, 0x60, 0x1D, 0xA0, 0x17, 0x80,
		0x54, 0x80, 0x5F, 0x80, 0x7E, 0xA0, 0x14, 0xA0,
		0x4A, 0x60, 0x0E, 0x00, 0x22, 0x40, 0x10, 0x00,
		0x10, 0x00, 0x14, 0x00, 0x0C, 0x00, 0x0C, 0x00
};
#else
	u8 model_data[64] = {0xAA,0x00,0xB6,0xC0,0xB7,0xE0,0xB9,0x30,
			     0xBA,0xF0,0xBB,0x90,0xBC,0xA0,0xBD,0x40,
			     0xBE,0x20,0xBF,0x40,0xC0,0xD0,0xC3,0xF0,
			     0xC6,0xE0,0xCC,0x90,0xD1,0xA0,0xD7,0x30,
			     0x01,0xC0,0x0E,0xC0,0x2C,0x00,0x1C,0x00,
			     0x00,0xC0,0x46,0x00,0x51,0xE0,0x31,0xE0,
			     0x22,0x80,0x2B,0xE0,0x13,0xC0,0x15,0xA0,
			     0x10,0x20,0x10,0x20,0x0E,0x00,0x0E,0x00};//you need to get model data from maxim integrated
#endif


static int max17058_write_reg(struct i2c_client *client, u8 reg, u16 value)
{
	int ret;

	ret = i2c_smbus_write_word_data(client, reg, swab16(value));

	if (ret < 0)
		dev_err(&client->dev, "%s: err %d\n", __func__, ret);

	return ret;
}

static int max17058_read_reg(struct i2c_client *client, u8 reg)
{
	int ret;

	ret = i2c_smbus_read_word_data(client, reg);

	if (ret < 0)
		dev_err(&client->dev, "%s: err %d\n", __func__, ret);

	return ret;
}
//check POR
static int max17058_check_por(struct i2c_client *client)
{
	u16 val;

	val = max17058_read_reg(client, max17058_STATUS_REG);
	val = swab16(val)&0x0100;

  	return val;
}
//get chip version
static u16 max17058_get_version(struct i2c_client *client)
{
	u16 fg_version = 0;
	u16 chip_version = 0;

	fg_version = max17058_read_reg(client, max17058_VER_REG);
	chip_version = swab16(fg_version);
  
	GAUGE_INFO("%s: max17058 Fuel-Gauge Ver 0x%04x\n", __func__, chip_version);
	return chip_version;
}

static void prepare_to_load_model(struct i2c_client *client) {
	u16 msb;
	u16 check_times = 0;
	u8 unlock_test_OCV_1/*MSB*/, unlock_test_OCV_2/*LSB*/;
	u16 chip_version;
	u16 val;
	int ret = 0;
	  
	do{
		//Step1:unlock model access, enable access to OCV and table registers
		ret = max17058_write_reg(client, max17058_MODEL_ACCESS_REG, max17058_MODEL_ACCESS_UNLOCK);
		if (ret < 0){
			GAUGE_ERR("failed to Unlock Model Access in step 1\n");
			return;
		}
		//Step2:Read OCV, verify Model Access Unlocked
		msleep(100);  
        	msb = max17058_read_reg(client, max17058_OCV_REG);//read OCV 
        	if (msb < 0){
			GAUGE_ERR("failed to read OCV in step 2\n");
			return;
		}
        
		unlock_test_OCV_1 = (msb)&(0x00FF);//"big endian":low byte save MSB
		unlock_test_OCV_2 = ((msb)&(0xFF00))>>8;         
        
		if(check_times++ >= 3) {//avoid of while(1)
			check_times = 0;
			GAUGE_ERR("failed to ulock the model...");
			break;
		}
	}while ((unlock_test_OCV_1==0xFF)&&(unlock_test_OCV_2==0xFF));//verify Model Access Unlocked
	/*
		The OCV Register will be modified during the process of loading the custom
		model.  Read and store this value so that it can be written back to the 
		device after the model has been loaded. do it for only the first time after power up or chip reset
	*/
	if(por_flag ==1){
		original_OCV_1 = unlock_test_OCV_1;//"big endian":low byte save to MSB
		original_OCV_2 = unlock_test_OCV_2;
		por_flag = 0; 
		//clear POR bit
		val = max17058_read_reg(client, max17058_STATUS_REG);
		val = swab16(val)& 0xFEFF;
		max17058_write_reg(client,max17058_STATUS_REG,val);
	}

	/******************************************************************************
		Step 2.5.1 MAX17058/59 Only
		To ensure good RCOMP_Seg values in MAX17058/59, a POR signal has to be sent to
		MAX17058. The OCV must be saved before the POR signal is sent for best Fuel Gauge
		performance.for chip version 0x0011 only 
	*******************************************************************************/
	chip_version = max17058_get_version(client);
	if(chip_version == 0x0011){
	do {
		ret = max17058_write_reg(client, max17058_CMD_REG, max17058_POR_CMD );
		if (ret < 0){
		GAUGE_ERR("failed to send POR Command in step 2.5.1\n");
		return;
		}

		ret = max17058_write_reg(client, max17058_MODEL_ACCESS_REG, max17058_MODEL_ACCESS_UNLOCK);
		if (ret < 0){
			GAUGE_ERR("failed to send Unlock Command in step 2.5.1\n");
			return;
		}
		msleep(100);
		msb = max17058_read_reg(client, max17058_OCV_REG);
		if (msb < 0){
			GAUGE_ERR("failed to Read OCV in step 2.5.1\n");
			return;
		}		
		unlock_test_OCV_1 = (msb)&(0x00FF);//"big endian":low byte save MSB
		unlock_test_OCV_2 = ((msb)&(0xFF00))>>8;

		if(check_times++ >= 3){//avoid of while(1)
			check_times = 0;
			GAUGE_ERR("time out3...");
			break;
		}
	}while ((unlock_test_OCV_1==0xFF)&&(unlock_test_OCV_2==0xFF));
  }

    //Step3: Write OCV
    //only for max17058/1/3/4, 
    //do nothing for MAX17058

    //Step4: Write RCOMP to its Maximum Value
    // only for max17058/1/3/4
    // max17058_write_reg(client,0x0C, 0xFF00);
    //do nothing for MAX17058
}

static void load_model(struct i2c_client *client) {	
	int i = 0;

   /******************************************************************************
	Step 5. Write the Model
	Once the model is unlocked, the host software must write the 64 byte model
	to the device. The model is located between memory 0x40 and 0x7F.
	The model is available in the INI file provided with your performance
	report. See the end of this document for an explanation of the INI file.
	Note that the table registers are write-only and will always read
	0xFF. Step 9 will confirm the values were written correctly.
	*/
	
	for (i = 0; i < 4; i += 1) {
			if (i2c_smbus_write_i2c_block_data(client,
				(MAX17058_TABLE+i*16), 16,
					&model_data[i*0x10]) < 0) {
				GAUGE_ERR("%s: error writing model data:\n", __func__);
				return;
			}
		}
#if 0
	int k=0;
	u16 value = 0;
	//Once the model is unlocked, the host software must write the 64 bytes model to the device
	for(k=0;k<0x40;k+=2)
	{
		value = (model_data[k]<<8)+model_data[k+1];
		//The model is located between memory 0x40 and 0x7F
		max17058_write_reg(client, 0x40+k, value);
	}
#endif

	//Write RCOMPSeg (for MAX17048/MAX17049 only)
	/*for(k=0;k<0x10;k++){
	    max17058_write_reg(client,0x80, 0x0080);
	}*/
}

static bool verify_model_is_correct(struct i2c_client *client) {
	u8 SOC_1, SOC_2;
	u16 msb;
	int ret = 0;
	
	//msleep(200);//Delay at least 150ms(max17058/1/3/4 only)

	//Step 7. Write OCV:write(reg[0x0E], INI_OCVTest_High_Byte, INI_OCVTest_Low_Byte)
	ret = max17058_write_reg(client, 0x0E, INI_OCVTEST);
	if (ret < 0){
		GAUGE_ERR("failed to write INI_OCVTEST in step 7\n");
		return ret;
	}
	//Step 7.1 Disable Hibernate (MAX17048/49 only)
	//max17058_write_reg(client,0x0A,0x0);

	//Step 7.2. Lock Model Access (MAX17048/49/58/59 only)
	ret = max17058_write_reg(client, max17058_MODEL_ACCESS_REG, max17058_MODEL_ACCESS_LOCK);
	if (ret < 0){
		GAUGE_ERR("failed to Lock Model in step 7.2\n");
		return ret;
	}
	//Step 8: Delay between 150ms and 600ms, delaying beyond 600ms could cause the verification to fail
	msleep(500);
 
	//Step 9. Read SOC register and compare to expected result
	msb = max17058_read_reg(client, max17058_SOC_REG);
	if (msb < 0){
		GAUGE_ERR("failed to Read SOC register in Step 9\n");
		return ret;
	}

	SOC_1 = (msb)&(0x00FF);//"big endian":low byte save MSB
	SOC_2 = ((msb)&(0xFF00))>>8;
	
	if(SOC_1 >= INI_SOCCHECKA && SOC_1 <= INI_SOCCHECKB) {
		GAUGE_INFO("####model was loaded successfully####\n");
		return true;
	}
	else {		
		GAUGE_ERR("!!!!model was NOT loaded successfully!!!!\n");
		return false; 
	}   
}

static void cleanup_model_load(struct i2c_client *client) {	
	u16 original_ocv=0;
	int ret = 0;
	original_ocv = ((u16)((original_OCV_1)<<8)+(u16)original_OCV_2);
	//step9.1, Unlock Model Access (MAX17048/49/58/59 only): To write OCV, requires model access to be unlocked
	ret = max17058_write_reg(client,max17058_MODEL_ACCESS_REG, max17058_MODEL_ACCESS_UNLOCK);
	if (ret < 0){
		GAUGE_ERR("failed to Lock Model Access in step 9.1\n");
		return;
	}

	//step 10 Restore CONFIG and OCV: write(reg[0x0C], INI_RCOMP, Your_Desired_Alert_Configuration)
	ret = max17058_write_reg(client,max17058_RCOMP_REG, 0x941C);//RCOMP0=94 , battery empty Alert threshold = 4% -> 0x1C
	if (ret < 0){
		GAUGE_ERR("failed to Restore Config in step 10\n");
		return;
	}
	
	ret = max17058_write_reg(client,max17058_OCV_REG, original_ocv); 
	if (ret < 0){
		GAUGE_ERR("failed to Restore in step 10\n");
		return;
	}
	//step 10.1 Restore Hibernate (MAX17048/49 only)
	//do nothing for MAX17058
    
	//step 11 Lock Model Access
	ret = max17058_write_reg(client,max17058_MODEL_ACCESS_REG, max17058_MODEL_ACCESS_LOCK);
	if (ret < 0){
		GAUGE_ERR("failed to Lock Model Access in step 11\n");
		return;
	}
	//step 12,//delay at least 150ms before reading SOC register
	mdelay(200); 
}

static void handle_model(struct i2c_client *client,int load_or_verify) {
	bool model_load_ok = false;
	int status;
	int check_times = 0;
    
	status = max17058_check_por(client);
	if(status)
	{
		GAUGE_INFO("POR happens,need to load model data\n");
		por_flag = 1;
	}
	else
	{
		GAUGE_INFO("POR does not happen,don't need to load model data\n");
		return;
	}
    
	do {
		if(load_or_verify == LOAD_MODEL) {		
		// Steps 1-4		
	    	prepare_to_load_model(client);
		// Step 5
		load_model(client);
		}
		// Steps 6-9
		model_load_ok = verify_model_is_correct(client);
		if(!model_load_ok) {
			load_or_verify = LOAD_MODEL;
		}
		if(check_times++ >= 3) {
			check_times = 0;
			GAUGE_INFO("max17058 handle model :time out1...");
			break;
		}
    	} while(!model_load_ok);

	// Steps 10-12
	cleanup_model_load(client);
}

static int max17058_get_temp(struct i2c_client *client)
{
#ifdef MAX17058_REG_POWERSUPPLY
	struct max17058_chip *chip = i2c_get_clientdata(client);
	union power_supply_propval val;

	if (!chip->bms_psy)
		chip->bms_psy = power_supply_get_by_name("battery");

	if(!chip->bms_psy)
	{
		GAUGE_ERR("%s get battery power_supply error!\n", __func__);
		return 25;//defult 25C
	}
	
	chip->bms_psy->get_property(chip->bms_psy, POWER_SUPPLY_PROP_TEMP, &val);

	val.intval = val.intval/10;
	GAUGE_INFO("%s, temp = %d\n", __func__, val.intval);
	
	return val.intval;
#else
	return 25;
#endif
}

//update 
static void update_rcomp(struct i2c_client *client) 
{
	int NewRCOMP;
	u16 cfg=0;
	u8 temp=0;

	temp = max17058_get_temp(client);
	if(temp > 20) 
	{
		NewRCOMP = INI_RCOMP + ((temp - 20) * TempCoHot)/INI_RCOMP_FACTOR;
	}else if(temp <20) 
	{
		NewRCOMP = INI_RCOMP +  ((temp - 20) * TempCoCold)/INI_RCOMP_FACTOR;
	}else 
	{
		NewRCOMP = INI_RCOMP;
	}
	
	if(NewRCOMP > 0xFF)
	{
		NewRCOMP = 0xFF;
	}else if(NewRCOMP <0) 
	{
		NewRCOMP = 0;
	}	
	cfg=(NewRCOMP<<8)|0x1c;//soc alert:4%   
	max17058_write_reg(client, 0x0c, cfg);
	msleep(150);
}
#ifdef MAX17058_REG_POWERSUPPLY
static int max17058_get_property(struct power_supply *psy,
			    enum power_supply_property psp,
			    union power_supply_propval *val)
{
	struct max17058_chip *chip = container_of(psy,
				struct max17058_chip, fgbattery);

	switch (psp) {
	case POWER_SUPPLY_PROP_STATUS:
		val->intval = chip->status;
		break;
	case POWER_SUPPLY_PROP_ONLINE:
		val->intval = chip->online;
		break;
	case POWER_SUPPLY_PROP_VOLTAGE_NOW:
		val->intval = chip->vcell;
		break;
	case POWER_SUPPLY_PROP_CAPACITY:
		val->intval = chip->soc;
		break;
	default:
		return -EINVAL;
	}
	return 0;
}
#endif
static void max17058_reset(struct i2c_client *client)
{
	max17058_write_reg(client, max17058_CMD_REG, max17058_POR_CMD);
}

static void max17058_get_vcell(struct i2c_client *client)
{
	struct max17058_chip *chip = i2c_get_clientdata(client);
	u16 fg_vcell = 0;
	u32 vcell_mV = 0;

	fg_vcell = max17058_read_reg(client, max17058_VCELL_REG);
	vcell_mV = (u32)(((fg_vcell & 0xFF)<<8) + ((fg_vcell & 0xFF00)>>8))*5/64;//78125uV/(1000*1000) = 5/64 mV/cell
	chip->vcell = vcell_mV;

	GAUGE_INFO("%s: chip->vcell = %d mV\n", __func__, chip->vcell);
}

static void max17058_get_soc(struct i2c_client *client)
{
	struct max17058_chip *chip = i2c_get_clientdata(client);
	u16 fg_soc = 0;
	u8 soc_count = 0;

	fg_soc = max17058_read_reg(client, max17058_SOC_REG);
	if(fg_soc < 0){
		GAUGE_ERR("%s failed to get soc\n", __func__);
	}

	chip->soc = ((u16)(fg_soc & 0xFF)<<8) + ((u16)(fg_soc & 0xFF00)>>8);
	GAUGE_INFO("%s: chip->soc = %d, fg_soc = %d\n", __func__, chip->soc, fg_soc);

	if(INI_BITS == 19) {
	    chip->soc = chip->soc/512;
	}else if(INI_BITS == 18){
	    chip->soc = chip->soc/256;
	}

	if(chip->soc>100)	chip->soc = 100;
	if((soc_count++)%5 == 0){
	    soc_count = 0;
	    GAUGE_INFO("%s: Get SOC = %d\n", __func__, chip->soc);
	}
}

#ifdef MAX17058_REG_POWERSUPPLY
static void max17058_get_online(struct i2c_client *client)
{
	struct max17058_chip *chip = i2c_get_clientdata(client);

	if (chip->pdata->battery_online)
		chip->online = chip->pdata->battery_online();
	else
		chip->online = 1;
}

static void max17058_get_status(struct i2c_client *client)
{
	struct max17058_chip *chip = i2c_get_clientdata(client);

	if (!chip->pdata->charger_online || !chip->pdata->charger_enable) {
		chip->status = POWER_SUPPLY_STATUS_UNKNOWN;
		return;
	}

	if (chip->pdata->charger_online()) {
		if (chip->pdata->charger_enable())
			chip->status = POWER_SUPPLY_STATUS_CHARGING;
		else
			chip->status = POWER_SUPPLY_STATUS_NOT_CHARGING;
	} else {
		chip->status = POWER_SUPPLY_STATUS_DISCHARGING;
	}

	if (chip->soc > max17058_BATTERY_FULL)
	    	chip->status = POWER_SUPPLY_STATUS_FULL;
}
#endif
static void max17058_work(struct work_struct *work)
{
	struct max17058_chip *chip;

	chip = container_of(work, struct max17058_chip, work.work);

	max17058_get_vcell(chip->client);
	max17058_get_soc(chip->client);
	update_rcomp(chip->client);//update rcomp periodically
#ifdef MAX17058_REG_POWERSUPPLY
	if(0)
	{
		max17058_get_online(chip->client);
		max17058_get_status(chip->client);
	}
#endif
	handle_model(chip->client, LOAD_MODEL);
	GAUGE_INFO("%s v=%d, soc=%d\n", __func__, chip->vcell, chip->soc);

	schedule_delayed_work(&chip->work, max17058_DELAY);
}

//static void max17058_handle_work(struct work_struct *work)
//{
//	struct max17058_chip *chip;
//
//	chip = container_of(work, struct max17058_chip, work.work);

//	handle_model(chip->client, LOAD_MODEL);

//	schedule_delayed_work(&chip->hand_work, max17058_DELAY);
//}

int max17058_read_percentage(void)
{
	int soc=0;

	//GAUGE_INFO("%s start\n", __func__);
	max17058_get_soc(g_max17058_chip->client);
	soc = g_max17058_chip->soc;
	return soc;
}
int max17058_read_current(void)
{
	int curr=0;

	curr += 0x10000;
	return curr;
}
int max17058_read_volt(void)
{
	uint16_t volt;
	
	//GAUGE_INFO("%s start\n", __func__);
	max17058_get_vcell(g_max17058_chip->client);
	volt = g_max17058_chip->vcell;
	GAUGE_INFO("%s, volt = %d\n", __func__, volt);
	return volt;
}
int max17058_read_temp(void)
{
	return 250;
}

static ssize_t batt_switch_name(struct switch_dev *sdev, char *buf)
{
	return sprintf(buf, "%s\n", "max17058");
}

#ifdef MAX17058_REG_POWERSUPPLY
static enum power_supply_property max17058_battery_props[] = {
	POWER_SUPPLY_PROP_STATUS,
	POWER_SUPPLY_PROP_ONLINE,
	POWER_SUPPLY_PROP_VOLTAGE_NOW,
	POWER_SUPPLY_PROP_CAPACITY,
};
#endif
static int max17058_probe(struct i2c_client *client,
			const struct i2c_device_id *id)
{
	struct i2c_adapter *adapter = to_i2c_adapter(client->dev.parent);			
	struct max17058_chip *chip;
	int ret;
	u32 test_major_flag=0;
	struct asus_bat_config bat_cfg;

	GAUGE_INFO("%s +++\n", __func__);

	if (!i2c_check_functionality(adapter, I2C_FUNC_SMBUS_BYTE))
		return -EIO;

	chip = kzalloc(sizeof(*chip), GFP_KERNEL);
	if (!chip)
		return -ENOMEM;

	chip->client = client;
#ifdef MAX17058_REG_POWERSUPPLY
	chip->pdata = client->dev.platform_data;
#endif
	i2c_set_clientdata(client, chip);
#ifdef MAX17058_REG_POWERSUPPLY
	chip->fgbattery.name		= "max17058_fgauge";
	chip->fgbattery.type		= POWER_SUPPLY_TYPE_BATTERY;
	chip->fgbattery.get_property	= max17058_get_property;
	chip->fgbattery.properties	= max17058_battery_props;
	chip->fgbattery.num_properties	= ARRAY_SIZE(max17058_battery_props);

	ret = power_supply_register(&client->dev, &chip->fgbattery);
	if (ret) {
		GAUGE_INFO("failed: power supply register\n");
		kfree(chip);
		return ret;
	}

	chip->bms_psy = power_supply_get_by_name("battery");
	if (!chip->bms_psy) {
		GAUGE_ERR("ma17058 battery power supply not found deferring probe\n");
		//return -EPROBE_DEFER;
	}
#endif
	
	//turn to jiffeys
	bat_cfg.polling_time = 0;
	bat_cfg.critical_polling_time = 0;
	bat_cfg.polling_time *= HZ;
	bat_cfg.critical_polling_time *= HZ;
	ret = asus_battery_init(bat_cfg.polling_time, bat_cfg.critical_polling_time, test_major_flag);
	if (ret)
		GAUGE_ERR("asus_battery_init fail\n");

	max17058_get_version(client);
	max17058_write_reg(client, 0x0c, 0x941C);
	handle_model(client, LOAD_MODEL);

  	g_max17058_chip = chip;
	INIT_DELAYED_WORK(&chip->work, max17058_work);
	//INIT_DELAYED_WORK(&chip->hand_work, max17058_handle_work);
	schedule_delayed_work(&chip->work, 0);
	//schedule_delayed_work(&chip->hand_work, 0);

	max17058_tbl.read_percentage = max17058_read_percentage;
	max17058_tbl.read_current = max17058_read_current;
	max17058_tbl.read_volt = max17058_read_volt;
	max17058_tbl.read_temp = max17058_read_temp;

	ret = asus_register_power_supply(&client->dev, &max17058_tbl);
	if (ret)
                GAUGE_ERR("asus_register_power_supply fail\n");

	/* register switch device for battery information versions report */
	max17058_batt_dev.name = "battery";
	max17058_batt_dev.print_name = batt_switch_name;
	if (switch_dev_register(&max17058_batt_dev) < 0)
		GAUGE_ERR("%s: fail to register battery switch\n", __func__);

	GAUGE_INFO("%s ---\n", __func__);
	return 0;
}

static int max17058_remove(struct i2c_client *client)
{
	struct max17058_chip *chip = i2c_get_clientdata(client);
#ifdef MAX17058_REG_POWERSUPPLY
	power_supply_unregister(&chip->fgbattery);
#endif
	cancel_delayed_work(&chip->work);
	kfree(chip);
	return 0;
}

#ifdef CONFIG_PM
static int max17058_suspend(struct i2c_client *client,
		pm_message_t state)
{
	struct max17058_chip *chip = i2c_get_clientdata(client);

	GAUGE_INFO("%s +++\n", __func__);
	cancel_delayed_work(&chip->work);
	return 0;
}

static int max17058_resume(struct i2c_client *client)
{
	struct max17058_chip *chip = i2c_get_clientdata(client);

	GAUGE_INFO("%s +++\n", __func__);
	schedule_delayed_work(&chip->work, max17058_DELAY);
	return 0;
}

#else

#define max17058_suspend NULL
#define max17058_resume NULL

#endif /* CONFIG_PM */

//static struct of_device_id max17058_match_table[] = {
//	{ .compatible = "max,max17058-fg"},
//	{ },
//};

static const struct i2c_device_id max17058_id[] = {
	{ "max17058_batt", 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, max17058_id);

static struct i2c_driver max17058_i2c_driver = {
	.driver	= {
		.name	= "max17058_batt",
//		.of_match_table	= max17058_match_table,
	},
	.probe		= max17058_probe,
	.remove		= max17058_remove,
	.suspend		= max17058_suspend,
	.resume		= max17058_resume,
	.id_table		= max17058_id,
};
//module_i2c_driver(max17058_i2c_driver);

static struct i2c_board_info max17058_board_info[] = {
	{
		I2C_BOARD_INFO("max17058", 0x36),
	},
};

static int __init max17058_init(void)
{
	int ret;

	GAUGE_INFO("%s +++\n", __func__);
	if (Read_PROJ_ID()!=PROJ_ID_ZX550ML) {
		GAUGE_INFO("Project version is NOT ZX550ML, so donot init\n");
		return 0;
	}

	ret = i2c_add_driver(&max17058_i2c_driver);
	if (ret)
		GAUGE_ERR("%s: i2c_add_driver failed\n", __func__);

//	ret = i2c_register_board_info(2, max17058_board_info, ARRAY_SIZE(max17058_board_info));
//	if (ret)
//		GAUGE_ERR("%s: i2c_register_board_info failed\n", __func__);

	GAUGE_INFO("%s ---\n", __func__);
	return ret;
}
late_initcall(max17058_init);
static void max17058_exit(void)
{
	i2c_del_driver(&max17058_i2c_driver);
}
module_exit(max17058_exit);

MODULE_AUTHOR("maxim integrated");
MODULE_DESCRIPTION("max17058 Fuel Gauge");
MODULE_LICENSE("GPL");
