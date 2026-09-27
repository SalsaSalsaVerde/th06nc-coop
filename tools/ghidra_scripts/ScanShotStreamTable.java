// Parses a shot-firing tier's stream array (docs/36/43/49): each record is
// 0x24 bytes; prints, per record, the mode byte at +0x1f and the sprite-id
// short at +0x20 (already the final global sprite id per docs/36, no extra
// offset needed). Run via analyzeHeadless ... -postScript
// ScanShotStreamTable.java <baseAddr> <recordCount>
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.mem.Memory;

public class ScanShotStreamTable extends GhidraScript {

    @Override
    public void run() throws Exception {
        String[] args = getScriptArgs();
        if (args.length < 2) {
            println("Usage: ScanShotStreamTable <baseAddr> <recordCount>");
            return;
        }
        Address base = currentProgram.getAddressFactory().getAddress(args[0]);
        int count = Integer.parseInt(args[1]);
        Memory mem = currentProgram.getMemory();
        final int kStride = 0x24;

        for (int i = 0; i < count; i++) {
            Address recAddr = base.add((long) i * kStride);
            byte modeByte = mem.getByte(recAddr.add(0x1f));
            short spriteId = mem.getShort(recAddr.add(0x20));
            short typeTag = mem.getShort(recAddr.add(0x22));
            println("record " + i + " @ " + recAddr + "  mode=" + (modeByte & 0xff) +
                    "  spriteId=" + spriteId + "  typeTag=" + typeTag);
        }
    }
}
