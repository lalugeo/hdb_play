//STEP 1. Import required packages
import java.sql.*;
import java.util.concurrent.ExecutorService;  
import java.util.concurrent.Executors;

class IrrCalc  implements Runnable{
	private Connection conn;
	private double contract_id;
	private int contract_size;
	private int contract_order;

 public void run(){  
   System.out.println("task one"); 
	double irr=0;
	try{
		irr=this.GetIrr(this.conn,this.contract_id,this.contract_size);
	}catch(SQLException ex){System.out.println("err for contract[" + contract_order + "]= " + ex.getMessage());}
	catch(Exception ex){System.out.println("err for contract[" + contract_order + "]= " + ex.getMessage());}
	finally{
	
		System.out.println("irr found for contract[" + contract_order + "] = "  + irr + "\n");
	}
 }  

	public IrrCalc(Connection conn,double contract_id,int contract_size,int contract_order){
		this.conn=conn;
		this.contract_id=contract_id;
		this.contract_size=contract_size;
		this.contract_order=contract_order;
	}

	
public double GetIrr(Connection conn, double contract_id,int contract_size) throws SQLException,Exception{

	int k=0;
	double irr;
try{

	Statement stmt= conn.createStatement();
	String sql;
      	sql = "SELECT days_between(\"PAYMENT_DATE\",to_date('1990-01-01')) \"PAYMENT_DATE\",\"AMOUNT_PRINCIPAL\" from IRT.\"irt.cfkernel.tables::CF.CashFlowSimple\" where \"CONTRACT_ID\"=" + contract_id;
      ResultSet rs = stmt.executeQuery(sql);

      //STEP 5: Extract data from result set
        double total=0.0;
	double[][] cashflow=new double[contract_size][2];
      while(rs.next()){
         //Retrieve by column name
	cashflow[k][0]=rs.getDouble("PAYMENT_DATE");
	cashflow[k][1]=rs.getDouble("AMOUNT_PRINCIPAL");
	k++;
      }
	
	
      //STEP 6: Clean-up environment
      rs.close();
      stmt.close();
	irr=this.IRR(cashflow);
}catch(Exception ex){
	throw new Exception("error at " + k + " for size=" + contract_size + ". " + ex.getMessage());
}
	return irr;
}
 public double IRR(double[][] cashflow) throws Exception{
                boolean irr_found=false;
                double irr_next_val=1;
                double tried_npv=0;
                double desired_npv=0;
                double irrn_1=0.2;
                double irrn_2=0.25;
                double npvn_2=0;
                double npvn_1=0;
                for(int i=0;i<100;i++){
                        if(i==0){
                                irr_next_val=0.25;
                        }else if (i==1){
                                irr_next_val=0.2;
                        }else{
                                irr_next_val=this.secant_method(irrn_1,npvn_1,irrn_2,npvn_2);
                        }
                        tried_npv=this.irr_try(cashflow,irr_next_val);
                        irrn_2=irrn_1;
                        irrn_1=irr_next_val;
                        npvn_2=npvn_1;
                        npvn_1=tried_npv;

                        if((tried_npv-desired_npv)<0.01 && (tried_npv-desired_npv)>-0.01){
                                irr_found=true;
                                break;
                        }
                }
                if(!irr_found){
                        //throw new Exception("no irr found after 100 tried");
                        System.out.println("no irr found after 100 tried");
                }
                return irr_next_val;
        }
        private double irr_try(double[][] cashflow,double possible_rate){
                double npv=0;
                double tmp;
                for(int i=0;i<cashflow.length;i++){
                        tmp=this.npv_part(cashflow[i][1],cashflow[i][0],cashflow[0][0],possible_rate);
                        npv=npv + tmp;
                }
                return npv;
        }
 private double npv_part(double cashflow_amount,double di,double d1,double rate){
                double ret_npv_part;
                double n=(di-d1)/365;
                double denom=Math.pow((1+ rate),n);
                ret_npv_part = cashflow_amount/denom;
                return ret_npv_part;
        }


        private double secant_method(double irrn_1,double npvn_1,double irrn_2,double npvn_2){
                double next_irr;
                next_irr=irrn_1 - (npvn_1* ((irrn_1-irrn_2)/(npvn_1 - npvn_2)));
                return next_irr;
        }
}  
public class lg{
   // JDBC driver name and database URL
   static final String JDBC_DRIVER = "com.sap.db.jdbc.Driver";  
   static final String DB_URL = "jdbc:sap://lt1master:30015/?currentschema=IRT";

   //  Database credentials
   static final String USER = "AFL_ACCESS";
   static final String PASS = "Initial123";
   
   public static void main(String[] args) throws SQLException{
   Connection conn = null;
   Statement stmt = null;
   try{
      //STEP 2: Register JDBC driver
      Class.forName("com.sap.db.jdbc.Driver");

      //STEP 3: Open a connection
      System.out.println("Connecting to database...");
      conn = DriverManager.getConnection(DB_URL,USER,PASS);

      //STEP 4: Execute a query
      System.out.println("Creating statement...");
      stmt = conn.createStatement();
      String sql;
      sql = "SELECT \"CONTRACT_ID\",count(*) \"PAY_COUNT\" from IRT.\"irt.cfkernel.tables::CF.CashFlowSimple\" group by \"CONTRACT_ID\" ";
      ResultSet rs = stmt.executeQuery(sql);

      //STEP 5: Extract data from result set
	double[][] contract_ids= new double[1000001][3]; 
	long startTime = System.nanoTime(); 
	int k=0;
      while(rs.next()){
         //Retrieve by column name
         contract_ids[k][0]  = rs.getDouble("CONTRACT_ID");
         contract_ids[k][1]  = rs.getDouble("PAY_COUNT");
	 k++;
      }
      rs.close();
      stmt.close();
	ExecutorService executor = Executors.newFixedThreadPool(100);
	for(int i =0; i<100;i++){
		//contract_ids[i][2]=GetIrr(conn, contract_ids[i][0],(int)contract_ids[i][1]);
		Runnable worker = new IrrCalc(conn, contract_ids[i][0],(int)contract_ids[i][1],i);  
		executor.execute(worker);
	}	
	executor.shutdown();  
        while (!executor.isTerminated()) {   } 

	long estimatedTime = System.nanoTime() - startTime;
        System.out.format("time elapsed: %f secs\n" ,(estimatedTime/1000000000.0) );

      //STEP 6: Clean-up environment
      conn.close();
   }catch(SQLException se){
      //Handle errors for JDBC
      se.printStackTrace();
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


}//end FirstExample
