#include "finders.h"
#include <stdio.h>
#include <stdlib.h>
#include <gcrypt.h>
#define MAXBUFLEN 60000
#define VERSION 999 //id for the validation
#define FILTER_TYPE 0// 0=> RSG overworld (Practice Seeds); 1=> village only; 2=> shipwreck only; 3 => jungle only; 4 => coinflip (classic)
#define DEBUG 0
#include <string.h>
#include <time.h>
#include "./minecraft_nether_gen_rs.h"

void int64ToChar(unsigned char a[], int64_t n) {
  a[0] = (n >> 56) & 0xFF;
  a[1] = (n >> 48) & 0xFF;
  a[2] = (n >> 40) & 0xFF;
  a[3] = (n >> 32) & 0xFF;
  a[4] = (n >> 24) & 0xFF;
  a[5] = (n >> 16) & 0xFF;
  a[6] = (n >> 8) & 0xFF;
  a[7] = n & 0xFF;
}

uint64_t charTo64bitNum(char a[]) {
  uint64_t n = (unsigned long) (a[7] & 0xFF);
  n |= (unsigned long) (a[6] & 0xFF) << 8;
  n |= (unsigned long) (a[5] & 0xFF) << 16;
  n |= (unsigned long) (a[4] & 0xFF) << 24;
  n |= (unsigned long) (a[3] & 0xFF) << 32;
  n |= (unsigned long) (a[2] & 0xFF) << 40;
  n |= (unsigned long) (a[1] & 0xFF) << 48;
  n |= (unsigned long) (a[0] & 0xFF) << 56;
  return n;
}

void print64(int64_t n){
  unsigned char chars[8];
  int64ToChar(chars, n);
  int i;
  for (i = 0; i < 8; i++){
    printf("%02x", chars[i]); /* print the result */
  }
  return;
}

void print32(unsigned int n){
  unsigned char chars[4];
  chars[0] = (n >> 24) & 0xFF;
  chars[1] = (n >> 16) & 0xFF;
  chars[2] = (n >> 8) & 0xFF;
  chars[3] = n & 0xFF;
  int i;
  for (i = 0; i < 4; i++){
    printf("%02x", chars[i]); /* print the result */
  }
  return;
}

long l2norm(long x1, long z1, long x2, long z2){
  return (x1-x2)*(x1-x2) + (z1-z2)*(z1-z2);
}

void verification_token(uint64_t timestamp, unsigned int vrs, int high_int, int lower_int, int64_t seed, uint64_t IV, uint64_t timestamp2){
  printf("Verification Token:\n");
  print64(IV);
  printf("-");
  print32(high_int);
  printf("-");
  print32(lower_int);
  printf("-");
  print64(timestamp);
  printf("-");
  print32(vrs);
  printf("-");
  print64(seed);
  printf("-%ld",timestamp2-timestamp);
  printf("\n");
  return;
}

uint64_t rand64()
{
  uint64_t rv = 0;
  int c,i;
  FILE *fp;
  fp = fopen("/dev/urandom", "r");

  for (i=0; i < sizeof(rv); i++) {
     do {
       c = fgetc(fp);
     } while (c < 0);
     rv = (rv << 8) | (c & 0xff);
  }
  fclose(fp);
  return rv;
}

//THE FILTERS: ethos these output a 0 for fail 1 for succeed and check either lower48 bits (fast filter) or top16 (slow filter)

//this is classic FSG fastion without biome (pos/pos bastion, tells you neg/pos or pos/neg fortress)
char netherchecker(int64_t seed, int* fortressQuadrant){
  //return true if the nether is good (3 structures within -128 to 128 ignoring neg/neg) at 
  unsigned long modulus = 1ULL << 48;
  unsigned long AA = 341873128712;
  unsigned long BB = 132897987541;
  int bastionCount = 0;
  *fortressQuadrant = 0;
  int64_t fakeseed = (seed + 30084232ULL) ^ 0x5deece66dUL;
  int64_t chunkx = next(&fakeseed, 31) % 23;
  int64_t chunkz = next(&fakeseed, 31) % 23;
  int structureType = (next(&fakeseed, 31) % 5)  >= 2;
  bastionCount += structureType;
  if (chunkx > 8 || chunkz > 8 || structureType == 0){
    return 0;
  }
  int gotfort = 0;
  fakeseed = (seed + 30084232UL - AA) ^ 0x5deece66dUL;
  chunkx = next(&fakeseed, 31) % 23;
  chunkz = next(&fakeseed, 31) % 23;
  structureType = (next(&fakeseed, 31) % 5)  >= 2;
  bastionCount += structureType;
  if (structureType == 0){
    *fortressQuadrant = -1;
  }
  if (chunkx >= 19 && chunkz <= 8 && structureType == 0){
    return 1;
  }

  fakeseed = (seed + 30084232UL - BB) ^ 0x5deece66dUL;
  chunkx = next(&fakeseed, 31) % 23;
  chunkz = next(&fakeseed, 31) % 23;
  structureType = (next(&fakeseed, 31) % 5)  >= 2;
  bastionCount += structureType;
  if (structureType == 0){
    *fortressQuadrant = 1;
  }
  if (chunkx <= 8 && chunkz >= 19 && structureType == 0){
    return 1;
  }

  return 0;
}

//checks for basalt deltas at bastion location
char bastionbiome(uint64_t seed){
  int64_t fakeseed = (seed + 30084232ULL) ^ 0x5deece66dUL;
  int64_t chunkx = next(&fakeseed, 31) % 23;
  int64_t chunkz = next(&fakeseed, 31) % 23;
  NetherGen* netherGen=create_new_nether(seed);  
  NetherBiomes biome=get_biome(netherGen,chunkx*16,0,chunkz*16);
  if (biome!=WarpedForest){
    delete(netherGen);
    return 0;
  }
  delete(netherGen);
  return 1;
}

//casts a drag net looking for any ravine start that will be low, wide, and flat
int ravinePositionAndQuality(int64_t seed){
  int64_t fakeseed, carvea, carveb;
  int carver_offset = 0;
  long chx, chz, i;
  long magmax, magmaz, magmay;
  long partialy, maxLength;
  float temp, pitch, width;
  for (chx = -4; chx < 16; chx++){
    for (chz = -4; chz < 16; chz++){
      fakeseed = (seed + carver_offset) ^ 0x5deece66dL;
      carvea = nextLong(&fakeseed);
      carveb = nextLong(&fakeseed);
      fakeseed = ((chx * carvea) ^ (chz * carveb) ^ (seed + carver_offset)) ^ 0x5deece66dL;
      fakeseed = fakeseed & 0xFFFFFFFFFFFF;
      temp = nextFloat(&fakeseed);
      if (temp < .02){
        Pos pos;
        magmax = chx * 16 + nextInt(&fakeseed, 16);
        partialy = nextInt(&fakeseed, 40) + 8;
        magmay = 20 + nextInt(&fakeseed, partialy);
        magmaz = chz * 16 + nextInt(&fakeseed, 16);
        pos.x = magmax;
        pos.z = magmaz;
        nextFloat(&fakeseed);
        pitch = (nextFloat(&fakeseed) - 0.5F) * 2.0F / 8.0F;
        temp = nextFloat(&fakeseed);
        width = (temp*2.0F + nextFloat(&fakeseed))*2.0F;
        maxLength = 112L - nextInt(&fakeseed, 28);
        if ( magmay < 25 && (pitch < 0.11 && pitch > -.11 ) && width > 2.5){
          return 1;
        }
      }
    }
  }
  return 0;
}

//casts a drag net for low, wide, flat, and ocean (not warm)
int ravineBiome(int64_t seed, LayerStack* gp){
  int64_t fakeseed, carvea, carveb;
  int carver_offset = 0;
  long chx, chz, i;
  long magmax, magmaz, magmay;
  long partialy, maxLength;
  float temp, pitch, width;
  for (chx = -4; chx < 16; chx++){
    for (chz = -4; chz < 16; chz++){
      fakeseed = (seed + carver_offset) ^ 0x5deece66dL;
      carvea = nextLong(&fakeseed);
      carveb = nextLong(&fakeseed);
      fakeseed = ((chx * carvea) ^ (chz * carveb) ^ (seed + carver_offset)) ^ 0x5deece66dL;
      fakeseed = fakeseed & 0xFFFFFFFFFFFF;
      temp = nextFloat(&fakeseed);
      if (temp < .02){
        Pos pos;
        magmax = chx * 16 + nextInt(&fakeseed, 16);
        partialy = nextInt(&fakeseed, 40) + 8;
        magmay = 20 + nextInt(&fakeseed, partialy);
        magmaz = chz * 16 + nextInt(&fakeseed, 16);
        pos.x = magmax;
        pos.z = magmaz;
        nextFloat(&fakeseed);
        pitch = (nextFloat(&fakeseed) - 0.5F) * 2.0F / 8.0F;
        temp = nextFloat(&fakeseed);
        width = (temp*2.0F + nextFloat(&fakeseed))*2.0F;
        maxLength = 112L - nextInt(&fakeseed, 28);
        if ( magmay < 25 && (pitch < 0.11 && pitch > -.11 ) && width > 2.5){
          int biomeAtPos = getBiomeAtPos(gp, pos);
          if (isOceanic(biomeAtPos) && (biomeAtPos !=  lukewarm_ocean &&  biomeAtPos != deep_lukewarm_ocean && biomeAtPos != warm_ocean && biomeAtPos != deep_warm_ocean) ){
            return 1;
          }
        }
      }
    }
  }
  return 0;
}

//Puts a village within 0 to 96
int villageLocation(int64_t lower48){
  const StructureConfig sconf = VILLAGE_CONFIG;
  int valid;
  Pos p = getStructurePos(sconf, lower48, 0, 0, &valid);
  if (!valid || p.x > 96 || p.z > 96){
    return 0;
  }
  return 1;
}

//Puts a jungle temple within 0 to 96
int jungleLocation(int64_t lower48){
  const StructureConfig sconf = JUNGLE_PYRAMID_CONFIG;
  int valid;
  Pos p = getStructurePos(sconf, lower48, 0, 0, &valid);
  if (!valid || p.x > 96 || p.z > 96){
    return 0;
  }
  return 1;
}

int shipwreckLocationAndType(int64_t seed){ //we will presume ocean not beach (test for speed)
  int valid3;
  const StructureConfig sconf_shipwreck = SHIPWRECK_CONFIG;
  Pos p3 = getStructurePos(sconf_shipwreck, seed, 0, 0, &valid3);
  if (!valid3 || p3.x >= 96 || p3.z >= 96){
    return 0;
  }
  unsigned long modulus = 1UL << 48;
  int64_t fakeseed, carvea, carveb;
  int shipchunkx, shipchunkz, portOceanType, portNormalY, portNormalType;
  int shiptype;
  shipchunkx = p3.x >> 4;
  shipchunkz = p3.z >> 4;
  fakeseed = (seed) ^ 0x5deece66dL;
  carvea = nextLong(&fakeseed);
  carveb = nextLong(&fakeseed);
  fakeseed = ((shipchunkx * carvea) ^ (shipchunkz * carveb) ^ seed) ^ 0x5deece66dL;
  fakeseed = fakeseed & 0xFFFFFFFFFFFF;
  fakeseed = (0x5deece66dUL*fakeseed + 11) % modulus ; //advance 2
  fakeseed = (0x5deece66dUL*fakeseed + 11) % modulus ;
  shiptype = (fakeseed >> 17) % 20;
  if (shiptype == 2 || shiptype == 5 || shiptype == 8 || shiptype == 12 || shiptype == 15 || shiptype == 18){
    return 0; //rejecting front only ships allowing all others
  }
  return 1;
}

int portalLocation(int64_t seed, int villageMode){
  const StructureConfig sconf_portal = RUINED_PORTAL_CONFIG;
  int valid2;
  Pos p2 = getStructurePos(sconf_portal, seed, 0, 0, &valid2);
  if (villageMode == 1){
    if (!valid2 || (p2.x <= 80 && p2.z <= 80) || p2.x >= 144 || p2.z >= 144){
      return 0;
    }
    return 1;
  }
  if (!valid2 || p2.x > 144 || p2.z > 144){
    return 0;
  }
  return 1;
}

int portalBiome(int64_t seed, LayerStack* gp){
  const StructureConfig sconf = RUINED_PORTAL_CONFIG;
  int valid;
  Pos p = getStructurePos(sconf, seed, 0, 0, &valid);
  int mc = MC_1_16;
  int biome = getBiomeAtPos(gp, p);
  if (isOceanic(biome)){
    return 0;
  }
  if (!isViableStructurePos(sconf.structType, mc, gp, seed, p.x, p.z)){
    return 0;
  }
  return 1;
}

//there are 3 biome classes to handle when filtering ruined_portal TYPE
int portalTypeJungle(int64_t seed){
  const StructureConfig sconf_portal = RUINED_PORTAL_CONFIG;
  int valid2, portalType;
  Pos p2 = getStructurePos(sconf_portal, seed, 0, 0, &valid2);
  unsigned long modulus = 1UL << 48;
  int portcx, portcz, portOceanType, portNormalY, portNormalType;
  float buriedFloat, bigOrSmall;
  int rawPortalType;
  int64_t fakeseed, carvea, carveb;
  portcx = p2.x >> 4;
  portcz = p2.z >> 4;
  fakeseed = (seed) ^ 0x5deece66dL;
  carvea = nextLong(&fakeseed);
  carveb = nextLong(&fakeseed);
  fakeseed = ((portcx * carvea) ^ (portcz * carveb) ^ seed) ^ 0x5deece66dL;
  fakeseed = fakeseed & 0xFFFFFFFFFFFF;
  next(&fakeseed, 31); //tossed out for jungle terrains (air gaps)
  //  buriedFloat = nextFloat(&fakeseed); // jungle never buries it
  bigOrSmall = nextFloat(&fakeseed); // 1/20 chance of being big
  rawPortalType = next(&fakeseed, 31); //once this is reduced mod 3 or 10 we know the type
  if (bigOrSmall < .05){
    return 1; //all three big ones have enough lava and we're not underground
  }
  portalType = rawPortalType % 10;
  if (portalType == 0 || portalType == 2 || portalType == 3 || portalType == 4 || portalType == 5 || portalType == 8){
    return 0; //6 small types have no lava
  }
  return 1; //this seed made it
}

int portalTypeOcean(int64_t seed){
  return 1; //in this case we're either buried or underwater so the lava doesn't matter
  //this is really just a chest and gold
  //IF the world demands portal Type filtering for the ocean/desert worlds I would do it here
}

int portalTypeNormal(int64_t seed){
  const StructureConfig sconf_portal = RUINED_PORTAL_CONFIG;
  int valid2, portalType;
  Pos p2 = getStructurePos(sconf_portal, seed, 0, 0, &valid2);
  int portcx, portcz, portOceanType, portNormalY, portNormalType;
  float buriedFloat, bigOrSmall;
  int rawPortalType;
  int64_t fakeseed, carvea, carveb;
  portcx = p2.x >> 4;
  portcz = p2.z >> 4;
  fakeseed = (seed) ^ 0x5deece66dL;
  carvea = nextLong(&fakeseed);
  carveb = nextLong(&fakeseed);
  fakeseed = ((portcx * carvea) ^ (portcz * carveb) ^ seed) ^ 0x5deece66dL;
  fakeseed = fakeseed & 0xFFFFFFFFFFFF;
  buriedFloat = nextFloat(&fakeseed); // 50/50 shot at being underground
  next(&fakeseed, 31); //tossed out for normal terrains (air gaps)
  bigOrSmall = nextFloat(&fakeseed); // 1/20 chance of being big
  rawPortalType = next(&fakeseed, 31); //once this is reduced mod 3 or 10 we know the type
  if (buriedFloat < .5){
    return 0;
  }
  if (bigOrSmall < .05){
    return 1; //all three big ones have enough lava and we're not underground
  }
  portalType = rawPortalType % 10;
  if (portalType == 0 || portalType == 2 || portalType == 3 || portalType == 4 || portalType == 5 || portalType == 8){
    return 0; //6 small types have no lava
  }
  return 1; //this seed made it
}

int strongholdAngle(int64_t seed, int fortressQuadrant){
  StrongholdIter sh;
  int mc = MC_1_16;
  Pos pos_sh = initFirstStronghold(&sh, mc, seed);
  long temp1, temp2, temp3;
  if (fortressQuadrant == -1){
    temp1 = l2norm(pos_sh.x, pos_sh.z, -1200L, 1200L);
    temp2 = l2norm(pos_sh.x, pos_sh.z, 1639L, 439L);
    temp3 = l2norm(pos_sh.x, pos_sh.z, -439L, -1639L);
    if ((temp1 > 300*300) && (temp2 > 300*300) && (temp3 > 300*300)){
      return 0;
    }
  }
  if (fortressQuadrant == 1){
    temp1 = l2norm(pos_sh.x, pos_sh.z, 1200L, -1200L);
    temp2 = l2norm(pos_sh.x, pos_sh.z, -1639L, -439L);
    temp3 = l2norm(pos_sh.x, pos_sh.z, 439L, 1639L);
    if ((temp1 > 300*300) && (temp2 > 300*300) && (temp3 > 300*300)){
      return 0;
    }
  }
  return 1;
}

int strongholdSlowCheck(int64_t seed, int fortressQuadrant, LayerStack* gp){
  StrongholdIter sh;
  int mc = MC_1_16;
  Pos pos_sh = initFirstStronghold(&sh, mc, seed);
  long sh_dist = 0xffffffffffff;
  long temp = 0;
  int i, N = 3;
  for (i = 1; i <= N; i++)
  {
    if (nextStronghold(&sh, gp, NULL) <= 0)
        break;
      if (fortressQuadrant == -1){
        temp = l2norm(sh.pos.x, sh.pos.z, -1200L, 1200L);
        if (temp < sh_dist){
          sh_dist = temp;
        }
      } else if (fortressQuadrant == 1){
        temp = l2norm(sh.pos.x, sh.pos.z, 1200L, -1200L);
        if (temp < sh_dist){
          sh_dist = temp;
        }
      }
  }
  if (sh_dist > 300*300){
    return 0;
  }
  return 1;
}

int valid_shipwreck_and_ravine_not_biome(int64_t lower48){
  if (ravinePositionAndQuality(lower48) == 0){
    return -7;
  }
  if (shipwreckLocationAndType(lower48) == 0){
    return -8;
  }
  return 1;
}

int valid_jungle_not_biome(int64_t lower48){
  if (jungleLocation(lower48) == 0){
    return -9;
  }
  if (portalTypeJungle(lower48) == 0){ //doing this now biome independent
    return -10;
  }
  return 1;
}

int valid_village_and_portal_not_biome(int64_t lower48){
  if (portalLocation(lower48, 1) == 0){ //village special portal
    return -3;
  }
  if (villageLocation(lower48) == 0){
    return -4;
  }
  if (portalTypeNormal(lower48) == 0){ //doing this now biome independent
    return -5;
  }
  return 1; //has filtered nether for pos/pos bast and 1 fotress + rough stronghold blind + portal location (the L) + village location (square)
}

int valid_structures_and_types(int64_t lower48, int* fortressQuadrant, int filter_style){
  if (netherchecker(lower48, fortressQuadrant) == 0){
    return -1;
  }

  if (strongholdAngle(lower48, *fortressQuadrant) == 0){
    return -2;
  }

  if (filter_style == 0){
    return 1; //good nether, good blind, no overworld
  }

  if (filter_style == 1){//village only
    //filters portal in L village in square and normal portal type
    return valid_village_and_portal_not_biome(lower48); //farmed out so we could do pre-planned or either
  }

  if (portalLocation(lower48, 0) == 0){ //for all future filters
    return -6;
  }

  if (filter_style == 2){//shipwreck
    return valid_shipwreck_and_ravine_not_biome(lower48);
  }

  if (filter_style == 3){//jungle only
    return valid_jungle_not_biome(lower48);
  }
  return 1;
}

int spawn_close(int64_t seed, LayerStack* gp){ //costs about 1/6 biomes...
  int mc = MC_1_16;
  Pos spawn = getSpawn(mc, gp, NULL, seed);
  if (spawn.x >= -48 && spawn.x <= 144 && spawn.z >= -48 && spawn.z <= 144){
    return 1;
  }
  return 0;
}

int spawn_medium(int64_t seed, LayerStack* gp){ //for shipwrecks we're ok being a little farther away for trees
  int mc = MC_1_16;
  Pos spawn = getSpawn(mc, gp, NULL, seed);
  if (spawn.x >= -100 && spawn.x <= 200 && spawn.z >= -100 && spawn.z <= 200){
    return 1;
  }
  return 0;
}

int villageBiome(int64_t seed, LayerStack* gp){
  const StructureConfig sconf = VILLAGE_CONFIG;
  int valid;
  Pos p = getStructurePos(sconf, seed, 0, 0, &valid);
  int mc = MC_1_16;
  if (!isViableStructurePos(sconf.structType, mc, gp, seed, p.x, p.z)){
    return 0;
  }
  return 1;
}

int shipwreckBiome(int64_t seed, LayerStack* gp){
  const StructureConfig sconf_shipwreck = SHIPWRECK_CONFIG;
  int valid3;
  Pos p3 = getStructurePos(sconf_shipwreck, seed, 0, 0, &valid3);
  int mc = MC_1_16;
  if (!isOceanic(getBiomeAtPos(gp, p3))){
    return 0;
  }
  if (!isViableStructurePos(sconf_shipwreck.structType, mc, gp, seed, p3.x, p3.z)){
    return 0;
  }
  return 1;
}

int jungleBiome(int64_t seed, LayerStack* gp){
  const StructureConfig sconf = JUNGLE_PYRAMID_CONFIG;
  int valid;
  Pos p = getStructurePos(sconf, seed, 0, 0, &valid);
  int mc = MC_1_16;
  if (!isViableStructurePos(sconf.structType, mc, gp, seed, p.x, p.z)){
    return 0;
  }
  return 1;
}

int valid_biomes(int64_t seed, int* fortressQuadrant, int filter_style, LayerStack* gp){
  applySeed(gp, seed);
  if (strongholdSlowCheck(seed, *fortressQuadrant, gp) == 0){
    return -20;
  }
  if (filter_style == 0){
    return 1;
  }

  if (portalBiome(seed, gp)==0){
    return -17;
  }
  if (filter_style == 1){
    if (spawn_close(seed, gp) == 0){
      return -11;
    }
    if (villageBiome(seed, gp)== 0){
      return -12;
    }
  }

  if (filter_style == 3){
    if (spawn_close(seed, gp) == 0){
      return -11;
    }
    if (jungleBiome(seed, gp)== 0){
      return -13;
    }
  }

  if (filter_style == 2){
    if (shipwreckBiome(seed, gp)== 0){
      return -14;
    }
    if (spawn_close(seed, gp) == 0){
      return -15;
    }
    if (ravineBiome(seed, gp) == 0){
      return -16;
    }
  }
  return 1;
}

int main () {
    uint64_t timestamp = (uint64_t) time(NULL);
    FILE *fp = fopen("csprng.c","rb"); //Uses SHA256 of this source code for key
    char source[MAXBUFLEN + 1];
    if (fp != NULL) {
        size_t newLen = fread(source, sizeof(char), MAXBUFLEN, fp);
        if ( ferror( fp ) != 0 ) {
            fputs("Error reading file", stderr);
        } else {
            source[newLen++] = '\0';
        }
        fclose(fp);
    }
    unsigned char *salsaKey;
    unsigned char *salsaKey2;   
    unsigned i;
    unsigned int l = gcry_md_get_algo_dlen(GCRY_MD_SHA256); /* get digest length (used later to print the result) */

    gcry_md_hd_t h;
    gcry_md_open(&h, GCRY_MD_SHA256, GCRY_MD_FLAG_SECURE); /* initialise the hash context */
    gcry_md_write(h, source, strlen(source)); /* hash some text */
    salsaKey = gcry_md_read(h, GCRY_MD_SHA256); /* get the result */

    unsigned char stampString[8];
    int64ToChar(stampString, timestamp);

    unsigned char longInput[41];
    for (i=0; i < 32; i++){
      longInput[i] = salsaKey[i];
    }
    for (i=0; i < 8; i++){
      longInput[32+i] = stampString[i];
    }
    if (DEBUG){
      printf("DEBUG: longInput:");
      for (i = 0; i < 40; i++){
        printf("%02x", longInput[i]); /* print the result */
      }
    }

    gcry_md_hd_t h2;
    gcry_md_open(&h2, GCRY_MD_SHA256, GCRY_MD_FLAG_SECURE); /* initialise the hash context */
    gcry_md_write(h2, longInput, 40); /* hash some text */
    salsaKey2 = gcry_md_read(h2, GCRY_MD_SHA256); /* get the result */

    gcry_error_t     gcryError;
    gcry_cipher_hd_t gcryCipherHd;
    size_t           index;

    unsigned char iniVector[8];
    uint64_t NONCE = rand64();
    int seedcounter = 0;
    int64ToChar(iniVector, NONCE);
    int biome_tolerance = 500;
    char descriptions[5][65] = {"(RSG overworld)", "(Village Only)", "(Shipwreck Only)", "(Jungle Only)", ""};
    printf("FSG v1.0.1 %s\n", descriptions[FILTER_TYPE]);

    int filterStyle = FILTER_TYPE;
    if (filterStyle == 4){ //Random village or shipwreck (classic)
      if (NONCE % 2 == 0){
        printf("Village Seed\n");
        filterStyle = 1;
      } else {
        printf("Shipwreck Seed\n");
        filterStyle = 2;
      }
    }

    if (filterStyle == 3){ //jungles
      biome_tolerance = 1000;
    }
    //biome_tolerance is tricky, some lower48s will never be satisfiable so we need a steam valve, but too impatient and 
    //the heavily biome dependent filters will need to be extra lucky
    //determine these through experimentation

    gcryError = gcry_cipher_open(
        &gcryCipherHd, // gcry_cipher_hd_t *
        GCRY_CIPHER_SALSA20,   // int
        GCRY_CIPHER_MODE_STREAM,   // int
        0);            // unsigned int
    if (gcryError)
    {
        printf("gcry_cipher_open failed:  %s/%s\n",
               gcry_strsource(gcryError),
               gcry_strerror(gcryError));
        return 1;
    }
    
    gcryError = gcry_cipher_setkey(gcryCipherHd, salsaKey2, 32);
    if (gcryError)
    {
        printf("gcry_cipher_setkey failed:  %s/%s\n",
               gcry_strsource(gcryError),
               gcry_strerror(gcryError));
        return 1;
    }
    
    if (DEBUG){
      printf("DEBUG: salsaKey2:");
      for (i = 0; i < 32; i++){
        printf("%02x", salsaKey2[i]); /* print the result */
      }
    }

    gcryError = gcry_cipher_setiv(gcryCipherHd, iniVector, 8);
    if (gcryError)
    {
        printf("gcry_cipher_setiv failed:  %s/%s\n",
               gcry_strsource(gcryError),
               gcry_strerror(gcryError));
        return 1;
    }
    if(DEBUG){
      printf("DEBUG: iniVector:");
      for (i = 0; i < 8; i++){
        printf("%02x", iniVector[i]); /* print the result */
      }
    }
    
    size_t txtLength = 8;
    uint64_t seed = 0;
    unsigned char * encBuffer = malloc(txtLength+1);
    unsigned char * textBuffer = malloc(txtLength+1);
    memset(textBuffer, 0, 9);
    int ii, ss;

    int has_lower = 0;
    uint64_t lower48 = 0;
    uint64_t upper16 = 0;
    int is_done = 0;
    int biome_counter = 0;
    initBiomes();
    LayerStack g;
    int mc = MC_1_16;
    setupGenerator(&g, mc);
    int ohcrap = 0;
    int fortressQuadrant = 0;

    for(ii=0; is_done < 1; ii++){
      gcryError = gcry_cipher_encrypt(
          gcryCipherHd, // gcry_cipher_hd_t
          encBuffer,    // void *
          txtLength,    // size_t
          textBuffer,    // const void *
          txtLength);   // size_t
      if (gcryError)
      {
          printf("gcry_cipher_decrypt failed:  %s/%s\n",
                gcry_strsource(gcryError),
                gcry_strerror(gcryError));
          return 1;
      }
      seedcounter++;
      seed = charTo64bitNum(encBuffer);
      int result = 0;
      if (has_lower < 1){
        lower48 = seed >> 16;
        result = valid_structures_and_types(lower48, &fortressQuadrant, filterStyle);
        if (result > 0 && bastionbiome(lower48) == 1){
          //bastion biomes are minorly affected by top 16 but are medium cost:
          has_lower = seedcounter;
          ohcrap = 0;
        } else {
          if ((seedcounter - biome_counter) % 1000000 == 0){
            printf(",");
            fflush(stdout);
          }
          //Could analyze the statistical cost of each filter here as they each give a different negative number
          has_lower = 0;
        }
      } else {
        upper16 = seed >> 48;
        seed = lower48 | (upper16 << 48);
        biome_counter += 1;
        if (biome_counter % 100 == 0){
          printf(".");
          fflush(stdout);          
        }
        if (bastionbiome(seed) == 0){
          has_lower = 0;
        } else {
          result = valid_biomes(seed, &fortressQuadrant, filterStyle, &g);
          if (result > 0){
            printf("\nSeed: %ld\n", seed);
            printf("Filtered %d seeds did %d biome checks\n", seedcounter, biome_counter);
            uint64_t timestamp2 = (uint64_t) time(NULL);
            verification_token(timestamp, VERSION, seedcounter, has_lower, seed, NONCE, timestamp2);
            is_done = 1;
          } else { //could analyze relative cost of each filter
            ohcrap += 1;
            is_done = 0;
            if (ohcrap >= biome_tolerance){
              has_lower = 0;
              ohcrap = 0;
              printf("@");
              fflush(stdout);
            }
          }
        }
      }
    }
    return 0;
}