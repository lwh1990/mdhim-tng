package mdhim;
import org.bridj.BridJ;
import org.bridj.Pointer;
import org.bridj.StructObject;
import org.bridj.ann.Field;
import org.bridj.ann.Library;
/**
 * Base message<br>
 * <i>native declaration : mdhim-tng-ycsb/src/messages.h:1059</i><br>
 * This file was autogenerated by <a href="http://jnaerator.googlecode.com/">JNAerator</a>,<br>
 * a tool written by <a href="http://ochafik.com/">Olivier Chafik</a> that <a href="http://code.google.com/p/jnaerator/wiki/CreditsAndLicense">uses a few opensource projects.</a>.<br>
 * For help, please visit <a href="http://nativelibs4java.googlecode.com/">NativeLibs4Java</a> or <a href="http://bridj.googlecode.com/">BridJ</a> .
 */
@Library("mdhim") 
public class mdhim_basem_t extends StructObject {
	static {
		BridJ.register();
	}
	@Field(0) 
	public int mtype() {
		return this.io.getIntField(this, 0);
	}
	@Field(0) 
	public mdhim_basem_t mtype(int mtype) {
		this.io.setIntField(this, 0, mtype);
		return this;
	}
	@Field(1) 
	public int server_rank() {
		return this.io.getIntField(this, 1);
	}
	@Field(1) 
	public mdhim_basem_t server_rank(int server_rank) {
		this.io.setIntField(this, 1, server_rank);
		return this;
	}
	@Field(2) 
	public int size() {
		return this.io.getIntField(this, 2);
	}
	@Field(2) 
	public mdhim_basem_t size(int size) {
		this.io.setIntField(this, 2, size);
		return this;
	}
	@Field(3) 
	public int index() {
		return this.io.getIntField(this, 3);
	}
	@Field(3) 
	public mdhim_basem_t index(int index) {
		this.io.setIntField(this, 3, index);
		return this;
	}
	@Field(4) 
	public int index_type() {
		return this.io.getIntField(this, 4);
	}
	@Field(4) 
	public mdhim_basem_t index_type(int index_type) {
		this.io.setIntField(this, 4, index_type);
		return this;
	}
	public mdhim_basem_t() {
		super();
	}
	public mdhim_basem_t(Pointer pointer) {
		super(pointer);
	}
}
