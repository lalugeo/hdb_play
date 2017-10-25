//STEP 1. Import required packages
import java.sql.*;
import java.util.Random;

public class ma{
   // JDBC driver name and database URL
   static final String JDBC_DRIVER = "com.sap.db.jdbc.Driver";  
   static final String DB_URL = "jdbc:sap://lt1master:30015/?currentschema=IRT";

   //  Database credentials
   static final String USER = "AFL_ACCESS";
   static final String PASS = "Initial123";
   
   public static void main(String[] args) {
   Connection conn = null;
   Statement stmt = null;
   try{
	double[][] cashflow;
	cashflow=new double[][]{
			{0,-123400},
			{365,36200},
			{730,54800},
			{1095,48100}
		};
	long startTime = System.nanoTime();
	
	double irr=IRR(cashflow);
	long estimatedTime = System.nanoTime() - startTime; 
	System.out.format("time elapsed: %f secs\n" ,(estimatedTime/1000000000.0) );
	System.out.println("irr=" + irr);
   }catch(Exception e){
      //Handle errors for Class.forName
      e.printStackTrace();
   }finally{
      //finally block used to close resources
      try{
         if(stmt!=null)
            stmt.close();
      }catch(SQLException se2){
      }// nothing we can do
      try{
         if(conn!=null)
            conn.close();
      }catch(SQLException se){
         se.printStackTrace();
      }//end finally try
   }//end try
   System.out.println("Goodbye!");
}//end main
	public static double IRR(double[][] cashflow) throws Exception{
		boolean irr_found=false;
		double irr_ubound=1;
		double irr_lbound=0;
		double irr_next_val=1;
		double tried_npv=0;
		double desired_npv=0;
		double irrn_1=0.2;
		double irrn_2=0.25;
		double npvn_2=0;
		double npvn_1=0;
		for(int i=0;i<100;i++){
			//irr_next_val=(irr_ubound + irr_lbound)/2;
			//if(i<2){
				//irr_next_val=Math.random();
			if(i==0){
				irr_next_val=0.25;
			}else if (i==1){
				irr_next_val=0.2;
			}else{
				irr_next_val=secant_method(irrn_1,npvn_1,irrn_2,npvn_2);
			}
			System.out.println("trying with " + irr_next_val + " for npv of " + tried_npv);
			tried_npv=irr_try(cashflow,irr_next_val);
			irrn_2=irrn_1;
			irrn_1=irr_next_val;
			npvn_2=npvn_1;
			npvn_1=tried_npv;

			if((tried_npv-desired_npv)<0.01 && (tried_npv-desired_npv)>-0.01){
				System.out.println("npv=0 reached\n");
				irr_found=true;
				break;
			}else{
			
					if(desired_npv<tried_npv){
						irr_ubound=irr_lbound;
						irr_lbound=irr_next_val;
					}else{
						irr_ubound=irr_ubound;
						irr_lbound=irr_next_val;
					}
			}
		}
		if(!irr_found){
			//throw new Exception("no irr found after 100 tried"); 	
			System.out.println("no irr found after 100 tried"); 	
		}
		return irr_next_val;
	}
	private static double irr_try(double[][] cashflow,double possible_rate){
		double npv=0;
		String p="0";
		double tmp;
		for(int i=0;i<cashflow.length;i++){
			tmp=npv_part(cashflow[i][1],cashflow[i][0],cashflow[0][0],possible_rate);
			p=p+" + " + cashflow[i][1] + "/pow((1 + " + possible_rate + ") , ((" + cashflow[i][0] + " - " + cashflow[0][0] + ")/365))"  ;
			npv=npv + tmp;
		}
		System.out.println(p + "\n");
		return npv;
	}
	

	private static double npv_part(double cashflow_amount,double di,double d1,double rate){
		double ret_npv_part;
		double n=(di-d1)/365;
		double denom=Math.pow((1+ rate),n);
		ret_npv_part = cashflow_amount/denom;
		return ret_npv_part;
	}


	private static double secant_method(double irrn_1,double npvn_1,double irrn_2,double npvn_2){
		double next_irr;
		next_irr=irrn_1 - (npvn_1* ((irrn_1-irrn_2)/(npvn_1 - npvn_2)));
		return next_irr;
	}
}//end FirstExample





















